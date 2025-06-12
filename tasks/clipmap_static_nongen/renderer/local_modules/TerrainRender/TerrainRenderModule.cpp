#include "TerrainRenderModule.hpp"

#include <glm/fwd.hpp>
#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "etna/DescriptorSet.hpp"


TerrainRenderModule::TerrainRenderModule()
  : terrainMgr(std::make_unique<TerrainManager>(10, 255))
{
}

void TerrainRenderModule::allocateResources()
{
  meshesParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(MeshesParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "terrainMeshesParams"});
  frustumPlanesBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(glm::vec4) * 6,
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "FrustumPlanesTerrain"});

  terrainMgr->loadTerrain();

  meshesParams = {
    .instancesCount = shader_uint(terrainMgr->getInstanceMeshes().size()),
    .relemsCount = shader_uint(terrainMgr->getRenderElements().size())};

  meshesParamsBuffer.map();
  std::memcpy(meshesParamsBuffer.data(), &meshesParams, sizeof(meshesParams));
  meshesParamsBuffer.unmap();

  oneShotCommands = etna::get_context().createOneShotCmdMgr();
}

void TerrainRenderModule::loadShaders()
{
  etna::create_program("culling_meshes", {TERRAIN_RENDER_NONGEN_MODULE_SHADERS_ROOT "culling.comp.spv"});

  etna::create_program(
    "terrain_render",
    {TERRAIN_RENDER_NONGEN_MODULE_SHADERS_ROOT "clipmap.vert.spv",
     TERRAIN_RENDER_NONGEN_MODULE_SHADERS_ROOT "clipmap.frag.spv"});
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

void TerrainRenderModule::loadMaps(std::vector<etna::Binding> terrain_bindings)
{
  auto shaderInfo = etna::get_shader_program("terrain_render");
  terrainSet =
    std::make_unique<etna::PersistentDescriptorSet>(etna::create_persistent_descriptor_set(
      shaderInfo.getDescriptorLayoutId(0), terrain_bindings, true));

  auto commandBuffer = oneShotCommands->start();
  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    terrainSet->processBarriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());
  oneShotCommands->submitAndWait(commandBuffer);

  texturesAmount = static_cast<uint32_t>(terrain_bindings.size() - 1);
}

void TerrainRenderModule::update(const RenderPacket& packet)
{
  terrainMgr->moveClipmap(packet.cameraWorldPosition);

  auto projView = packet.projView;

  glm::vec4 frustumPlanes[6] = {};

  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 2; j++)
    {
      frustumPlanes[i * 2 + j].x = projView[0][3] + (projView[0][i] * (j == 0 ? 1 : -1));
      frustumPlanes[i * 2 + j].y = projView[1][3] + (projView[1][i] * (j == 0 ? 1 : -1));
      frustumPlanes[i * 2 + j].z = projView[2][3] + (projView[2][i] * (j == 0 ? 1 : -1));
      frustumPlanes[i * 2 + j].w = projView[3][3] + (projView[3][i] * (j == 0 ? 1 : -1));

      frustumPlanes[i * 2 + j] = glm::normalize(frustumPlanes[i * 2 + j]);
    }
  }

  frustumPlanesBuffer.map();
  std::memcpy(frustumPlanesBuffer.data(), &frustumPlanes, sizeof(glm::vec4) * 6);
  frustumPlanesBuffer.unmap();
}

void TerrainRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params)
{
  auto& matricesBuffer = terrainMgr->getInstanceMatricesBuffer();

  {
    ETNA_PROFILE_GPU(cmd_buf, cullTerrain);
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
    cullTerrain(cmd_buf, cullingPipeline.getVkPipelineLayout(), packet, matricesBuffer);
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
    renderTerrain(cmd_buf, terrainRenderPipeline.getVkPipelineLayout(), packet, matricesBuffer);
  }
}

void TerrainRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  ImGui::End();
}

void TerrainRenderModule::cullTerrain(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Buffer& matrices_buffer)
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
     etna::Binding{4, matrices_buffer.genBinding()},
     etna::Binding{5, terrainMgr->getRelemInstanceOffsetsBuffer().genBinding()},
     etna::Binding{6, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{7, terrainMgr->getDrawCommandsBuffer().genBinding()},
     etna::Binding{8, meshesParamsBuffer.genBinding()},
     etna::Binding{9, frustumPlanesBuffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<glm::mat4x4>(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {packet.projView});


  cmd_buf.dispatch(
    (static_cast<uint32_t>(terrainMgr->getInstanceMeshes().size()) + 127) / 128, 1, 1);

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
  const etna::Buffer& matrices_buffer)
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
    shaderInfo.getDescriptorLayoutId(1),
    cmd_buf,
    {
      etna::Binding{0, matrices_buffer.genBinding()},
      etna::Binding{1, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {terrainSet->getVkSet(), vkSet}, {});

  cmd_buf.pushConstants<PushConstants>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    0,
    {{.projView = packet.projView, .texturesAmount = texturesAmount}});

  cmd_buf.drawIndexedIndirect(
    terrainMgr->getDrawCommandsBuffer().get(),
    0,
    static_cast<uint32_t>(terrainMgr->getRenderElements().size()),
    sizeof(vk::DrawIndexedIndirectCommand));
}
