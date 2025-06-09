#include "WaterRenderModule.hpp"

#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <tracy/Tracy.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "RenderPacket.hpp"
#include "WaterRender/shaders/WaterRenderParams.h"
#include "shaders/SubdivisionParams.h"
#include "shaders/WaterParams.h"


WaterRenderModule::WaterRenderModule()
  : cbt(std::make_unique<CBTree>(25))
  , displayParams(
      {.pixelsPerEdge = 15.0f,
       .subdivision = 5,
       .displacementVariance = 0.01f,
       .resolution = 16384.0f})
  , waterParams({.extent = shader_uvec2(256), .heightOffset = shader_float(0.3)})
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
  , merge(false)
{
}

WaterRenderModule::WaterRenderModule(WaterParams par)
  : cbt(std::make_unique<CBTree>(25))
  , displayParams(
      {.pixelsPerEdge = 15.0f,
       .subdivision = 3,
       .displacementVariance = 0.01f,
       .resolution = 65536.0f})
  , waterParams(par)
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
  , merge(false)
{
}

void WaterRenderModule::allocateResources()
{

  subdivisionParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(SubdivisionParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "subdivisionParams"});

  waterParamsBuffer = etna::get_context().createBuffer(
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

  waterParamsBuffer.map();
  std::memcpy(waterParamsBuffer.data(), &waterParams, sizeof(WaterParams));
  waterParamsBuffer.unmap();

  renderParamsBuffer.map();
  std::memcpy(renderParamsBuffer.data(), &renderParams, sizeof(WaterRenderParams));
  renderParamsBuffer.unmap();

  cbt->allocateResources();
}

void WaterRenderModule::loadShaders()
{
  etna::create_program(
    "subdivision_split",
    {
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "decoy.vert.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "subdivision_split.tesc.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "process.tese.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "water.frag.spv",
    });

  etna::create_program(
    "subdivision_merge",
    {
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "decoy.vert.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "subdivision_merge.tesc.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "process.tese.spv",
      WATER_RENDER_CBT_MODULE_SHADERS_ROOT "water.frag.spv",
    });

  cbt->loadShaders();
}

void WaterRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  subdivisionSplitPipeline = pipelineManager.createGraphicsPipeline(
    "subdivision_split",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .tessellationConfig = {.patchControlPoints = 1},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframe_enabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
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

  subdivisionMergePipeline = pipelineManager.createGraphicsPipeline(
    "subdivision_merge",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .tessellationConfig = {.patchControlPoints = 1},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframe_enabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
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

  cbt->setupPipelines();
}

void WaterRenderModule::loadMaps()
{
  cbt->load();
}

void WaterRenderModule::update(const RenderPacket& packet, float camera_fovy, float window_height)
{
  ZoneScoped;

  subdivisionParams.world = glm::scale(
    glm::translate(
      glm::identity<glm::mat4>(),
      glm::vec3(-displayParams.resolution / 2.0f, 0.0f, -displayParams.resolution / 2.0f)),
    glm::vec3(displayParams.resolution, 0, displayParams.resolution));

  subdivisionParams.view = packet.view;
  subdivisionParams.proj = packet.proj;
  subdivisionParams.projView = subdivisionParams.proj * subdivisionParams.view;
  subdivisionParams.worldView = subdivisionParams.view * subdivisionParams.world;
  subdivisionParams.worldProjView = subdivisionParams.proj * subdivisionParams.worldView;


  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 2; j++)
    {
      subdivisionParams.frustumPlanes[i * 2 + j].x =
        subdivisionParams.projView[0][3] + (subdivisionParams.projView[0][i] * (j == 0 ? 1 : -1));
      subdivisionParams.frustumPlanes[i * 2 + j].y =
        subdivisionParams.projView[1][3] + (subdivisionParams.projView[1][i] * (j == 0 ? 1 : -1));
      subdivisionParams.frustumPlanes[i * 2 + j].z =
        subdivisionParams.projView[2][3] + (subdivisionParams.projView[2][i] * (j == 0 ? 1 : -1));
      subdivisionParams.frustumPlanes[i * 2 + j].w =
        subdivisionParams.projView[3][3] + (subdivisionParams.projView[3][i] * (j == 0 ? 1 : -1));

      subdivisionParams.frustumPlanes[i * 2 + j] =
        glm::normalize(subdivisionParams.frustumPlanes[i * 2 + j]);
    }
  }

  subdivisionParams.lodFactor = getLodFactor(camera_fovy, window_height);

  subdivisionParams.varianceFactor =
    (displayParams.displacementVariance / 64.0f) * (displayParams.displacementVariance / 64.0f);
  subdivisionParams.tesselationFactor = 1u << displayParams.subdivision;

  subdivisionParamsBuffer.map();
  std::memcpy(subdivisionParamsBuffer.data(), &subdivisionParams, sizeof(SubdivisionParams));
  subdivisionParamsBuffer.unmap();
}

void WaterRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const RenderPacket& packet,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  {
    std::array bufferBarriers = {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = cbt->getDrawIndirectBuffer().get(),
      .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  cbt->prepareIndirect(cmd_buf);

  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .buffer = cbt->getDrawIndirectBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTessellationControlShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = cbt->getCBTBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  if (!merge)
  {
    ETNA_PROFILE_GPU(cmd_buf, renderSplitTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eGraphics, subdivisionSplitPipeline.getVkPipeline());
    splitWater(
      cmd_buf,
      subdivisionSplitPipeline.getVkPipelineLayout(),
      packet,
      water_map,
      water_normal_map,
      water_sampler,
      directional_lights_buffer,
      cubemap);
  }
  else
  {
    ETNA_PROFILE_GPU(cmd_buf, renderMergeTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eGraphics, subdivisionMergePipeline.getVkPipeline());
    mergeWater(
      cmd_buf,
      subdivisionMergePipeline.getVkPipelineLayout(),
      packet,
      water_map,
      water_normal_map,
      water_sampler,
      directional_lights_buffer,
      cubemap);
  }

  merge = !merge;

  {
    std::array bufferBarriers = {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTessellationControlShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = cbt->getCBTBuffer().get(),
      .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, reductCBT);
    cbt->reduct(cmd_buf);
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

    ImGui::SeparatorText("Subdivision Params");

    float pixelsPerEdge = displayParams.pixelsPerEdge;
    ImGui::DragFloat("Pixels Per Edge", &pixelsPerEdge, 0.01f, 0.5f, 64.0f);
    int subdivision = static_cast<int>(displayParams.subdivision);
    ImGui::DragInt("Subdivision Scale", &subdivision, 1.0f, 1, 10);
    float displacementVariance = displayParams.displacementVariance;
    ImGui::DragFloat("Displacement Variance", &displacementVariance, 0.01f, 0.0f, 64.0f);

    ImGui::SeparatorText("Water Map Params");
    float resolution = displayParams.resolution;
    ImGui::DragFloat("Resolution", &resolution, 10.0f, 1.0f, 131072.0f);

    displayParams = {
      .pixelsPerEdge = pixelsPerEdge,
      .subdivision = static_cast<std::uint32_t>(subdivision),
      .displacementVariance = displacementVariance,
      .resolution = resolution};
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

void WaterRenderModule::splitWater(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  auto shaderInfo = etna::get_shader_program("subdivision_split");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, cbt->getCBTBuffer().genBinding()},
     etna::Binding{1, subdivisionParamsBuffer.genBinding()},
     etna::Binding{2, waterParamsBuffer.genBinding()},
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

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {vkSet}, {});

  cmd_buf.pushConstants<RenderPacket>(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, {packet});

  cmd_buf.drawIndirect(cbt->getDrawIndirectBuffer().get(), 0, 1, 0);
}

void WaterRenderModule::mergeWater(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  auto shaderInfo = etna::get_shader_program("subdivision_merge");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, cbt->getCBTBuffer().genBinding()},
     etna::Binding{1, subdivisionParamsBuffer.genBinding()},
     etna::Binding{2, waterParamsBuffer.genBinding()},
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

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {vkSet}, {});

  cmd_buf.pushConstants<RenderPacket>(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, {packet});

  cmd_buf.drawIndirect(cbt->getDrawIndirectBuffer().get(), 0, 1, 0);
}

float WaterRenderModule::getLodFactor(float camera_fovy, float window_height)
{
  float targetSize = 2.0f * glm::tan(glm::radians(camera_fovy) / 2.0f) *
    static_cast<float>(1 << displayParams.subdivision) * displayParams.pixelsPerEdge /
    window_height;

  return -2.0f * glm::log2(targetSize) + 2.0f;
}
