#pragma once

#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>

#include "etna/ComputePipeline.hpp"
#include "scene/TerrainManager.hpp"
#include "shaders/MeshesParams.h"
#include "shaders/WaterParams.h"
#include "shaders/WaterRenderParams.h"
#include "modules/RenderPacket.hpp"


class WaterRenderModule
{
public:
  WaterRenderModule();
  explicit WaterRenderModule(WaterParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);

  void update(const RenderPacket& packet);

  void execute(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);


  void drawGui();

private:
  void cullWater(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const RenderPacket& packet);

  void renderWater(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const RenderPacket& packet,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);

private:
  std::unique_ptr<TerrainManager> terrainMgr;

  WaterParams params;
  etna::Buffer paramsBuffer;

  WaterRenderParams renderParams;
  etna::Buffer renderParamsBuffer;

  MeshesParams meshesParams;
  etna::Buffer meshesParamsBuffer;

  etna::Buffer frustumPlanesBuffer;

  etna::GraphicsPipeline waterRenderPipeline;
  etna::ComputePipeline cullingPipeline;
};
