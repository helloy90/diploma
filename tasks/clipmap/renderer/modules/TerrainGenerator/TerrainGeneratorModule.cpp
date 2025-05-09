#include "TerrainGeneratorModule.hpp"

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>


TerrainGeneratorModule::TerrainGeneratorModule()
  : maxNumberOfSamples(16)
{
}

TerrainGeneratorModule::TerrainGeneratorModule(uint32_t max_number_of_samples)
  : maxNumberOfSamples(max_number_of_samples)
{
}

void TerrainGeneratorModule::allocateResources(vk::Format map_format, vk::Extent3D extent)
{
  auto& ctx = etna::get_context();

  terrainMap = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = extent,
      .name = "terrain_map",
      .format = map_format,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
        vk::ImageUsageFlagBits::eStorage});

  paramsBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(TerrainGenerationParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainGenerationParams"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eMirroredRepeat,
      .name = "terrain_sampler"});

  params = {
    .extent = {extent.width, extent.height},
    .numberOfSamples = 2,
    .persistence = shader_float(0.3)};
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
        .colorAttachmentFormats = {terrainMap.getFormat()},
      }});
}

void TerrainGeneratorModule::execute()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    paramsBuffer.map();
    std::memcpy(paramsBuffer.data(), &params, sizeof(TerrainGenerationParams));
    paramsBuffer.unmap();

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

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

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void TerrainGeneratorModule::drawGui()
{
  ImGui::Begin("Application Settings");

  static ImU32 numberOfSamplesMin = 1;
  static ImU32 numberOfSamplesMax = maxNumberOfSamples;
  static float persistenceMin = 0.0f;
  static float persistenceMax = 1.0f;

  if (ImGui::CollapsingHeader("Terrain Generation"))
  {
    ImGui::SeparatorText("Generation parameters");
    ImGui::SliderScalar(
      "Number of samples",
      ImGuiDataType_U32,
      &params.numberOfSamples,
      &numberOfSamplesMin,
      &numberOfSamplesMax,
      "%u");
    ImGui::SliderScalar(
      "Persistence",
      ImGuiDataType_Float,
      &params.persistence,
      &persistenceMin,
      &persistenceMax,
      "%f");
    if (ImGui::Button("Regenerate Terrain"))
    {
      execute();
    }
  }

  ImGui::End();
}
