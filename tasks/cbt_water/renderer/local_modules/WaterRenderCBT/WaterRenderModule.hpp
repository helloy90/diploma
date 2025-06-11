#pragma once

#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/DescriptorSet.hpp>
#include <etna/OneShotCmdMgr.hpp>

#include "modules/RenderPacket.hpp"
#include "CBT/CBTree.hpp"
#include "shaders/SubdivisionParams.h"
#include "shaders/WaterParams.h"
#include "shaders/WaterRenderParams.h"


class WaterRenderModule
{
public:
  WaterRenderModule();
  explicit WaterRenderModule(WaterParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void loadMaps();

  void update(const RenderPacket& packet, float camera_fovy, float window_height);

  void execute(
    vk::CommandBuffer cmd_buf,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params,
      const RenderPacket& packet,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);


  void drawGui();

private:
  struct SubdivisionDisplayParams
  {
    float pixelsPerEdge;
    std::uint32_t subdivision;
    float displacementVariance;
    float resolution;
  };

private:
  void splitWater(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
      const RenderPacket& packet,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);
  void mergeWater(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
      const RenderPacket& packet,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);

  float getLodFactor(float camera_fovy, float window_height);

  void updateParams();

private:
  std::unique_ptr<CBTree> cbt;

  SubdivisionParams subdivisionParams;
  SubdivisionDisplayParams displayParams;
  etna::Buffer subdivisionParamsBuffer;

  etna::GraphicsPipeline subdivisionSplitPipeline;
  etna::GraphicsPipeline subdivisionMergePipeline;

  WaterParams waterParams;
  etna::Buffer waterParamsBuffer;

  WaterRenderParams renderParams;
  etna::Buffer renderParamsBuffer;

  bool merge;
};
