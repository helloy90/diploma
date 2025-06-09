#include "TerrainRenderModule.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "shaders/SubdivisionParams.h"


TerrainRenderModule::TerrainRenderModule()
  : cbt(std::make_unique<CBTree>(25))
  , displayParams(
      {.pixelsPerEdge = 15.0f,
       .subdivision = 3,
       .displacementVariance = 0.01f,
       .resolution = 65536.0f})
  , merge(false)
{
}

void TerrainRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(SubdivisionParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "subdivisionParams"});

  oneShotCommands = etna::get_context().createOneShotCmdMgr();

  cbt->allocateResources();
}

void TerrainRenderModule::loadShaders()
{
  etna::create_program(
    "subdivision_split",
    {
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "decoy.vert.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "subdivision_split.tesc.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "process.tese.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "terrain.frag.spv",
    });

  etna::create_program(
    "subdivision_merge",
    {
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "decoy.vert.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "subdivision_merge.tesc.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "process.tese.spv",
      TERRAIN_RENDER_CBT_MODULE_SHADERS_ROOT "terrain.frag.spv",
    });

  cbt->loadShaders();
}

void TerrainRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
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

  cbt->setupPipelines();
}

void TerrainRenderModule::loadMaps(std::vector<etna::Binding> terrain_bindings)
{
  auto splitShaderInfo = etna::get_shader_program("subdivision_split");
  terrainSplitSet =
    std::make_unique<etna::PersistentDescriptorSet>(etna::create_persistent_descriptor_set(
      splitShaderInfo.getDescriptorLayoutId(1), terrain_bindings, true));
  auto mergeShaderInfo = etna::get_shader_program("subdivision_merge");
  terrainMergeSet =
    std::make_unique<etna::PersistentDescriptorSet>(etna::create_persistent_descriptor_set(
      mergeShaderInfo.getDescriptorLayoutId(1), terrain_bindings, true));

  auto commandBuffer = oneShotCommands->start();
  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    terrainSplitSet->processBarriers(commandBuffer);
    terrainMergeSet->processBarriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());
  oneShotCommands->submitAndWait(commandBuffer);

  params.texturesAmount = static_cast<uint32_t>(terrain_bindings.size() - 1);

  cbt->load();
}

void TerrainRenderModule::update(const RenderPacket& packet, float camera_fovy, float window_height)
{
  ZoneScoped;

  params.world = glm::scale(
    glm::translate(
      glm::identity<glm::mat4>(),
      glm::vec3(-displayParams.resolution / 2.0f, 0.0f, -displayParams.resolution / 2.0f)),
    glm::vec3(displayParams.resolution, 0, displayParams.resolution));

  params.view = packet.view;
  params.proj = packet.proj;
  params.projView = params.proj * params.view;
  params.worldView = params.view * params.world;
  params.worldProjView = params.proj * params.worldView;


  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 2; j++)
    {
      params.frustumPlanes[i * 2 + j].x =
        params.projView[0][3] + (params.projView[0][i] * (j == 0 ? 1 : -1));
      params.frustumPlanes[i * 2 + j].y =
        params.projView[1][3] + (params.projView[1][i] * (j == 0 ? 1 : -1));
      params.frustumPlanes[i * 2 + j].z =
        params.projView[2][3] + (params.projView[2][i] * (j == 0 ? 1 : -1));
      params.frustumPlanes[i * 2 + j].w =
        params.projView[3][3] + (params.projView[3][i] * (j == 0 ? 1 : -1));

      params.frustumPlanes[i * 2 + j] = glm::normalize(params.frustumPlanes[i * 2 + j]);
    }
  }

  params.lodFactor = getLodFactor(camera_fovy, window_height);

  params.varianceFactor =
    (displayParams.displacementVariance / 64.0f) * (displayParams.displacementVariance / 64.0f);
  params.tesselationFactor = 1u << displayParams.subdivision;

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(SubdivisionParams));
  paramsBuffer.unmap();
}

void TerrainRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params)
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
    splitTerrain(cmd_buf, subdivisionSplitPipeline.getVkPipelineLayout());
  }
  else
  {
    ETNA_PROFILE_GPU(cmd_buf, renderMergeTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eGraphics, subdivisionMergePipeline.getVkPipeline());
    mergeTerrain(cmd_buf, subdivisionMergePipeline.getVkPipelineLayout());
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

void TerrainRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  if (ImGui::CollapsingHeader("Terrain Render"))
  {
    ImGui::SeparatorText("Subdivision Params");

    float pixelsPerEdge = displayParams.pixelsPerEdge;
    ImGui::DragFloat("Pixels Per Edge", &pixelsPerEdge, 0.01f, 0.5f, 64.0f);
    int subdivision = static_cast<int>(displayParams.subdivision);
    ImGui::DragInt("Subdivision Scale", &subdivision, 1.0f, 1, 10);
    float displacementVariance = displayParams.displacementVariance;
    ImGui::DragFloat("Displacement Variance", &displacementVariance, 0.01f, 0.0f, 64.0f);

    ImGui::SeparatorText("Terrain Map Params");
    float resolution = displayParams.resolution;
    ImGui::DragFloat("Resolution", &resolution, 10.0f, 1.0f, 131072.0f);

    displayParams = {
      .pixelsPerEdge = pixelsPerEdge,
      .subdivision = static_cast<std::uint32_t>(subdivision),
      .displacementVariance = displacementVariance,
      .resolution = resolution};
  }
  ImGui::End();
}

void TerrainRenderModule::splitTerrain(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto shaderInfo = etna::get_shader_program("subdivision_split");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, cbt->getCBTBuffer().genBinding()},
     etna::Binding{1, paramsBuffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {vkSet, terrainSplitSet->getVkSet()}, {});

  cmd_buf.drawIndirect(cbt->getDrawIndirectBuffer().get(), 0, 1, 0);
}

void TerrainRenderModule::mergeTerrain(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto shaderInfo = etna::get_shader_program("subdivision_merge");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, cbt->getCBTBuffer().genBinding()},
     etna::Binding{1, paramsBuffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {vkSet, terrainMergeSet->getVkSet()}, {});

  cmd_buf.drawIndirect(cbt->getDrawIndirectBuffer().get(), 0, 1, 0);
}

float TerrainRenderModule::getLodFactor(float camera_fovy, float window_height)
{
  float targetSize = 2.0f * glm::tan(glm::radians(camera_fovy) / 2.0f) *
    static_cast<float>(1 << displayParams.subdivision) * displayParams.pixelsPerEdge /
    window_height;

  return -2.0f * glm::log2(targetSize) + 2.0f;
}

void TerrainRenderModule::updateParams() {}
