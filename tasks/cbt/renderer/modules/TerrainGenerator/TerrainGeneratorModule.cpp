#include "TerrainGeneratorModule.hpp"

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>


TerrainGeneratorModule::TerrainGeneratorModule()
  : texturesAmount(8)
{
  cascades.reserve(texturesAmount);
  infos.reserve(texturesAmount);
}

TerrainGeneratorModule::TerrainGeneratorModule(uint32_t textures_amount)
  : texturesAmount(textures_amount)
{
  cascades.reserve(texturesAmount);
  infos.reserve(texturesAmount);
}

void TerrainGeneratorModule::allocateResources(vk::Format map_format, vk::Extent3D extent)
{
  auto& ctx = etna::get_context();

  for (uint32_t i = 0; i < texturesAmount; i++)
  {
    cascades.push_back(
      {.map = ctx.createImage(
         etna::Image::CreateInfo{
           .extent = extent,
           .name = fmt::format("terrain_map_{}", i),
           .format = map_format,
           .imageUsage = vk::ImageUsageFlagBits::eSampled |
             vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage}),
       .params =
         {.extent = {extent.width, extent.height},
          .damping = shader_uint(256u << i),
          .octaves = 3,
          .persistence = 0.3},
       .paramsBuffer = ctx.createBuffer(
         etna::Buffer::CreateInfo{
           .size = sizeof(TerrainGenerationParams),
           .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
           .memoryUsage = VMA_MEMORY_USAGE_AUTO,
           .allocationCreate = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
             VMA_ALLOCATION_CREATE_MAPPED_BIT,
           .name = fmt::format("terrainGenerationParams{}", i)})});
    infos.push_back({
      .extent = glm::ivec2((1u << (i + 6u))),
      .heightOffset = 0.6f,
      .heightAmplifier = 10.0f,
    });
  }

  infosBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = texturesAmount * sizeof(TerrainCascadeInfo),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "terrainInfos"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = texturesAmount * sizeof(TerrainCascadeInfo)});

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eMirroredRepeat,
      .name = "terrain_sampler"});
}

void TerrainGeneratorModule::loadShaders()
{
  etna::create_program(
    "terrain_generator",
    {TERRAIN_GENERATOR_MODULE_SHADERS_ROOT "decoy.vert.spv",
     TERRAIN_GENERATOR_MODULE_SHADERS_ROOT "generator.frag.spv"});
}

void TerrainGeneratorModule::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_generator",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {cascades[0].map.getFormat()},
      }});
}

void TerrainGeneratorModule::execute()
{
  transferHelper->uploadBuffer(*oneShotCommands, infosBuffer, 0, std::as_bytes(std::span(infos)));

  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    for (auto& cascade : cascades)
    {
      etna::set_state(
        commandBuffer,
        cascade.map.get(),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
    }

    etna::flush_barriers(commandBuffer);

    for (auto& [terrainMap, params, paramsBuffer] : cascades)
    {
      paramsBuffer.map();
      std::memcpy(paramsBuffer.data(), &params, sizeof(TerrainGenerationParams));
      paramsBuffer.unmap();


      auto extent = terrainMap.getExtent();
      glm::uvec2 glmExtent = {extent.width, extent.height};

      {
        etna::RenderTargetState state(
          commandBuffer,
          {{}, {glmExtent.x, glmExtent.y}},
          {{terrainMap.get(), terrainMap.getView({})}},
          {});

        auto shaderInfo = etna::get_shader_program("terrain_generator");
        auto set = etna::create_descriptor_set(
          shaderInfo.getDescriptorLayoutId(0),
          commandBuffer,
          {etna::Binding{0, paramsBuffer.genBinding()}});

        auto vkSet = set.getVkSet();

        commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          terrainGenerationPipeline.getVkPipelineLayout(),
          0,
          1,
          &vkSet,
          0,
          nullptr);

        commandBuffer.bindPipeline(
          vk::PipelineBindPoint::eGraphics, terrainGenerationPipeline.getVkPipeline());

        commandBuffer.draw(3, 1, 0, 0);
      }

      etna::set_state(
        commandBuffer,
        terrainMap.get(),
        vk::PipelineStageFlagBits2::eVertexShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);
    }

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void TerrainGeneratorModule::drawGui()
{
  ImGui::Begin("Application Settings");

  static bool infosChanged = false;

  if (ImGui::CollapsingHeader("Terrain Generation"))
  {
    ImGui::SeparatorText("Generation parameters");
    for (uint32_t i = 0; i < texturesAmount; i++)
    {
      if (ImGui::TreeNode(&cascades[i], "Cascade %d", i))
      {
        ImGui::SeparatorText("Texture info");
        auto& params = cascades[i].params;
        int damping = static_cast<int>(params.damping);
        ImGui::DragInt("Damping", &damping, 1.0f, 1, 8192, "%u");
        int octaves = static_cast<int>(params.octaves);
        ImGui::DragInt("Octaves", &octaves, 1.0f, 1, 32, "%u");
        float persistence = params.persistence;
        ImGui::DragFloat("Persistence", &persistence, 0.01f, 0.0f, 2.0f, "%f");

        params = {
          .extent = params.extent,
          .damping = shader_uint(damping),
          .octaves = shader_uint(octaves),
          .persistence = persistence,
        };

        ImGui::SeparatorText("Terrain info");
        auto& info = infos[i];
        int extent[] = {info.extent.x, info.extent.y};
        infosChanged = infosChanged || ImGui::DragInt2("Extent", extent, 1, 1, 131072);
        float heightOffset = info.heightOffset;
        infosChanged = infosChanged ||
          ImGui::DragFloat("Height Offset", &heightOffset, 0.01f, -10.0f, 10.0f, "%f");
        float heightAmplifier = info.heightAmplifier;
        infosChanged = infosChanged ||
          ImGui::DragFloat("Height Amplifier", &heightAmplifier, 0.01f, 1.0f, 1024.0f, "%f");

        info = {
          .extent = {extent[0], extent[1]},
          .heightOffset = heightOffset,
          .heightAmplifier = heightAmplifier,
        };

        ImGui::TreePop();
      }
    }
    if (ImGui::Button("Regenerate Terrain"))
    {
      ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());

      execute();
    }
  }

  if (infosChanged)
  {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
    transferHelper->uploadBuffer(*oneShotCommands, infosBuffer, 0, std::as_bytes(std::span(infos)));
    infosChanged = false;
  }

  ImGui::End();
}

std::vector<etna::Binding> TerrainGeneratorModule::getBindings(vk::ImageLayout layout) const
{
  std::vector<etna::Binding> bindings;
  bindings.reserve(texturesAmount + 1);

  for (uint32_t i = 0; i < texturesAmount; i++)
  {
    bindings.emplace_back(
      etna::Binding{0, cascades[i].map.genBinding(terrainSampler.get(), layout), i});
  }
  bindings.emplace_back(etna::Binding{1, infosBuffer.genBinding()});

  return bindings;
}
