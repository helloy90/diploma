#include "WaterRenderModule.hpp"

#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>


WaterRenderModule::WaterRenderModule()
  : terrainMgr(std::make_unique<TerrainManager>(7, 255))
  , params({.extent = shader_uvec2(256), .heightOffset = shader_float(0.3)})
  , renderParams(
      {.scatterColor = shader_vec4(0.016, 0.0736, 0.16, 1),
       .bubbleColor = shader_vec4(0, 0.02, 0.016, 1),
       .foamColor = shader_vec4(0.6, 0.5568, 0.492, 1),
       .roughness = shader_float(0.1),
       .reflectionStrength = shader_float(0.9),
       .wavePeakScatterStrength = shader_float(2.2),
       .scatterStrength = shader_float(1),
       .scatterShadowStrength = shader_float(0.7),
       .bubbleDensity = shader_float(1.3)})
{
}

WaterRenderModule::WaterRenderModule(WaterParams par)
  : params(par)
  , renderParams(
      //{.color = shader_vec4(0.4627450980, 0.7137254902, 0.7686274510, 1),
      {.scatterColor = shader_vec4(0.016, 0.0736, 0.16, 1),
       .bubbleColor = shader_vec4(0, 0.02, 0.016, 1),
       .foamColor = shader_vec4(0.6, 0.5568, 0.0492, 1),
       .roughness = shader_float(0.3),
       .reflectionStrength = shader_float(0.5),
       .wavePeakScatterStrength = shader_float(1),
       .scatterStrength = shader_float(1),
       .scatterShadowStrength = shader_float(0.5),
       .bubbleDensity = shader_float(1)})
{
}

void WaterRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(WaterParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "waterParams"});

  renderParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(WaterRenderParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "waterRenderParams"});

  meshesParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(MeshesParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "waterMeshesParams"});

  frustumPlanesBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(glm::vec4) * 6,
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "FrustumPlanesWater"});

  terrainMgr->loadTerrain();

  meshesParams = {
    .instancesCount = shader_uint(terrainMgr->getInstanceMeshes().size()),
    .relemsCount = shader_uint(terrainMgr->getRenderElements().size())};

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(WaterParams));
  paramsBuffer.unmap();

  renderParamsBuffer.map();
  std::memcpy(renderParamsBuffer.data(), &renderParams, sizeof(WaterRenderParams));
  renderParamsBuffer.unmap();

  meshesParamsBuffer.map();
  std::memcpy(meshesParamsBuffer.data(), &meshesParams, sizeof(meshesParams));
  meshesParamsBuffer.unmap();
}

void WaterRenderModule::loadShaders()
{
  etna::create_program("culling_meshes", {WATER_RENDER_MODULE_SHADERS_ROOT "culling.comp.spv"});

  etna::create_program(
    "water_render",
    {WATER_RENDER_MODULE_SHADERS_ROOT "clipmap.vert.spv",
     WATER_RENDER_MODULE_SHADERS_ROOT "water.frag.spv"});
}

void WaterRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = terrainMgr->getVertexFormatDescription(),
    }},
  };

  waterRenderPipeline = pipelineManager.createGraphicsPipeline(
    "water_render",
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
          .attachments = {{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
          }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {render_target_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cullingPipeline = pipelineManager.createComputePipeline("culling_meshes", {});
}

void WaterRenderModule::update(const RenderPacket& packet)
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

void WaterRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  {
    ETNA_PROFILE_GPU(cmd_buf, cullWaterMeshes);
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
    cullWater(cmd_buf, cullingPipeline.getVkPipelineLayout(), packet);
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, renderWater);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, waterRenderPipeline.getVkPipeline());
    renderWater(
      cmd_buf,
      waterRenderPipeline.getVkPipelineLayout(),
      packet,
      water_map,
      water_normal_map,
      water_sampler,
      directional_lights_buffer,
      cubemap);
  }
}

void WaterRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  static bool renderParamsChanged = false;

  if (ImGui::CollapsingHeader("Water Render"))
  {
    ImGui::SeparatorText("Render parameters");

    ImGuiColorEditFlags colorFlags =
      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha;

    float scatterColor[] = {
      renderParams.scatterColor.r,
      renderParams.scatterColor.g,
      renderParams.scatterColor.b,
    };
    float bubbleColor[] = {
      renderParams.bubbleColor.r,
      renderParams.bubbleColor.g,
      renderParams.bubbleColor.b,
    };
    float foamColor[] = {
      renderParams.foamColor.r,
      renderParams.foamColor.g,
      renderParams.foamColor.b,
    };

    float roughness = renderParams.roughness;
    float reflectionStrength = renderParams.reflectionStrength;
    float wavePeakScatterStrength = renderParams.wavePeakScatterStrength;
    float scatterStrength = renderParams.scatterStrength;
    float scatterShadowStrength = renderParams.scatterShadowStrength;
    float bubbleDensity = renderParams.bubbleDensity;

    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Scatter Color", scatterColor, colorFlags);
    renderParams.scatterColor = shader_vec4(scatterColor[0], scatterColor[1], scatterColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Bubbles Color", bubbleColor, colorFlags);
    renderParams.bubbleColor = shader_vec4(bubbleColor[0], bubbleColor[1], bubbleColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Foam Color", foamColor, colorFlags);
    renderParams.foamColor = shader_vec4(foamColor[0], foamColor[1], foamColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::DragFloat("Water Roughness", &roughness, 0.001f, 0.0f, 1.0f);
    renderParams.roughness = roughness;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Reflection Strength", &reflectionStrength, 0.1f, 0.0f, 500.0f);
    renderParams.reflectionStrength = reflectionStrength;
    renderParamsChanged =
      renderParamsChanged ||
      ImGui::DragFloat(
        "Water Wave Peak Scatter Strength", &wavePeakScatterStrength, 0.1f, 0.0f, 500.0f);
    renderParams.wavePeakScatterStrength = wavePeakScatterStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Scatter Strength", &scatterStrength, 0.1f, 0.0f, 500.0f);
    renderParams.scatterStrength = scatterStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Scatter Shadow Strength", &scatterShadowStrength, 0.1f, 0.0f, 500.0f);
    renderParams.scatterShadowStrength = scatterShadowStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Bubbles Density", &bubbleDensity, 0.1f, 0.0f, 500.0f);
    renderParams.bubbleDensity = bubbleDensity;
  }

  if (renderParamsChanged)
  {
    renderParamsBuffer.map();
    std::memcpy(renderParamsBuffer.data(), &renderParams, sizeof(WaterRenderParams));
    renderParamsBuffer.unmap();
    renderParamsChanged = false;
  }

  ImGui::End();
}

void WaterRenderModule::cullWater(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const RenderPacket& packet)
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

void WaterRenderModule::renderWater(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  ZoneScoped;
  if (!terrainMgr->getVertexBuffer())
  {
    return;
  }

  cmd_buf.bindVertexBuffers(0, {terrainMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(terrainMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  auto shaderInfo = etna::get_shader_program("water_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, terrainMgr->getInstanceMatricesBuffer().genBinding()},
     etna::Binding{1, terrainMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{2, paramsBuffer.genBinding()},
     etna::Binding{3, renderParamsBuffer.genBinding()},
     etna::Binding{
       4, water_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       5,
       water_normal_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       6,
       cubemap.genBinding(
         water_sampler.get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})},
     etna::Binding{7, directional_lights_buffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<RenderPacket>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    0,
    {packet});

  cmd_buf.drawIndexedIndirect(
    terrainMgr->getDrawCommandsBuffer().get(),
    0,
    static_cast<uint32_t>(terrainMgr->getRenderElements().size()),
    sizeof(vk::DrawIndexedIndirectCommand));
}
