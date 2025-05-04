#include "TerrainRenderModule.hpp"

#include <glm/fwd.hpp>
#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "../HeightParams.hpp"
#include "shaders/TerrainParams.h"


TerrainRenderModule::TerrainRenderModule()
  : terrainMgr(std::make_unique<TerrainManager>(7, 255))
  , terrainParams({
      .extent = shader_uvec2(4096),
      .chunk = shader_uvec2(16),
      .terrainInChunks = shader_uvec2(64, 64),
      .terrainOffset = shader_vec2(-512, -512),
    })
  , heightParams({.amplifier = shader_float(200.0f), .offset = shader_float(0.6f)})
{
}

TerrainRenderModule::TerrainRenderModule(TerrainParams par)
  : terrainMgr(std::make_unique<TerrainManager>(7, 255))
  , terrainParams(par)
  , heightParams({.amplifier = shader_float(200.0f), .offset = shader_float(0.6f)})
{
}

TerrainRenderModule::TerrainRenderModule(HeightParams par)
  : terrainMgr(std::make_unique<TerrainManager>(7, 255))
  , terrainParams(
      {.extent = shader_uvec2(4096),
       .chunk = shader_uvec2(16, 16),
       .terrainInChunks = shader_uvec2(64, 64),
       .terrainOffset = shader_vec2(-512, -512)})
  , heightParams(par)
{
}

void TerrainRenderModule::allocateResources()
{
  terrainParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(TerrainParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainParams"});

  heightParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(HeightParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainHeightParams"});

  meshesParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(HeightParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainMeshesParams"});

  terrainMgr->loadTerrain();

  meshesParams = {
    .instancesCount = shader_uint(terrainMgr->getInstanceMeshes().size()),
    .relemsCount = shader_uint(terrainMgr->getRenderElements().size())};

  terrainParamsBuffer.map();
  std::memcpy(terrainParamsBuffer.data(), &terrainParams, sizeof(TerrainParams));
  terrainParamsBuffer.unmap();

  heightParamsBuffer.map();
  std::memcpy(heightParamsBuffer.data(), &heightParams, sizeof(HeightParams));
  heightParamsBuffer.unmap();

  meshesParamsBuffer.map();
  std::memcpy(meshesParamsBuffer.data(), &meshesParams, sizeof(meshesParams));
  meshesParamsBuffer.unmap();
}

void TerrainRenderModule::loadShaders()
{
  etna::create_program("culling_meshes", {TERRAIN_RENDER_MODULE_SHADERS_ROOT "culling.comp.spv"});

  etna::create_program(
    "terrain_render",
    {TERRAIN_RENDER_MODULE_SHADERS_ROOT "clipmap.vert.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "clipmap.frag.spv"});
}

void TerrainRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = terrainMgr->getVertexFormatDescription(),
    }},
  };

  terrainRenderPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_render",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframe_enabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eClockwise,
          .lineWidth = 1.f,
        },
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats =
            {render_target_format, vk::Format::eR8G8B8A8Snorm, vk::Format::eR8G8B8A8Unorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cullingPipeline = pipelineManager.createComputePipeline("culling_meshes", {});
}

void TerrainRenderModule::update(const RenderPacket& packet)
{
  terrainMgr->moveClipmap(packet.cameraWorldPosition);
}

void TerrainRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
  cullTerrain(cmd_buf, cullingPipeline.getVkPipelineLayout());

  {
    ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
    renderTerrain(
      cmd_buf,
      terrainRenderPipeline.getVkPipelineLayout(),
      packet,
      terrain_map,
      terrain_normal_map,
      terrain_sampler);
  }
}

void TerrainRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  if (ImGui::CollapsingHeader("Terrain Render"))
  {
    ImGui::SeparatorText("Height Adjustment");
    ImGui::SliderFloat("Height Amplifier", &heightParams.amplifier, 0, 10000, "%.3f");
    ImGui::SliderFloat("Height Offset", &heightParams.offset, -1.0f, 1.0f, "%.5f");
    if (ImGui::Button("Apply"))
    {
      heightParamsBuffer.map();
      std::memcpy(heightParamsBuffer.data(), &heightParams, sizeof(HeightParams));
      heightParamsBuffer.unmap();
    }
  }

  ImGui::End();
}

void TerrainRenderModule::cullTerrain(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = terrainMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = terrainMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  auto shaderInfo = etna::get_shader_program("culling_meshes");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, terrainMgr->getRelemsBuffer().genBinding()},
     etna::Binding{1, terrainMgr->getBoundsBuffer().genBinding()},
     etna::Binding{2, terrainMgr->getMeshesBuffer().genBinding()},
     etna::Binding{3, terrainMgr->getInstanceMeshesBuffer().genBinding()},
     etna::Binding{4, terrainMgr->getInstanceMatricesBuffer().genBinding()},
     etna::Binding{5, terrainMgr->getRelemInstanceOffsetsBuffer().genBinding()},
     etna::Binding{6, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{7, terrainMgr->getDrawCommandsBuffer().genBinding()},
     etna::Binding{8, meshesParamsBuffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((terrainMgr->getInstanceMeshes().size() + 127) / 128, 1, 1);

  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = terrainMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .buffer = terrainMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }
}

void TerrainRenderModule::renderTerrain(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  ZoneScoped;
  if (!terrainMgr->getVertexBuffer())
  {
    return;
  }

  cmd_buf.bindVertexBuffers(0, {terrainMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(terrainMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  auto shaderInfo = etna::get_shader_program("terrain_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, terrainMgr->getInstanceMatricesBuffer().genBinding()},
      etna::Binding{1, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
      etna::Binding{2, heightParamsBuffer.genBinding()},
      etna::Binding{3, terrainParamsBuffer.genBinding()},
      etna::Binding{
        4, terrain_map.genBinding(terrain_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{
        5,
        terrain_normal_map.genBinding(
          terrain_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<glm::mat4x4>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eVertex,
    0,
    {packet.projView});

  cmd_buf.drawIndexedIndirect(
    terrainMgr->getDrawCommandsBuffer().get(),
    0,
    terrainMgr->getRenderElements().size(),
    sizeof(vk::DrawIndexedIndirectCommand));
}
