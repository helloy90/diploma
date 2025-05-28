#pragma once

#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/DescriptorSet.hpp>
#include <etna/OneShotCmdMgr.hpp>

#include "../RenderPacket.hpp"
#include "CBT/CBTree.hpp"
#include "shaders/SubdivisionParams.h"


class TerrainRenderModule
{
public:
  TerrainRenderModule();

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void loadMaps(std::vector<etna::Binding> terrain_bindings);

  void update(const RenderPacket& packet, float camera_fovy, float window_height);

  void execute(
    vk::CommandBuffer cmd_buf,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params);

  void drawGui();

private:
  void splitTerrain(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void mergeTerrain(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);

  float getLodFactor(float camera_fovy, float window_height);

  void updateParams();

private:
  struct SubdivisionDisplayParams
  {
    float pixelsPerEdge;
    std::uint32_t subdivision;
    float displacementVariance;
    float resolution;
    float verticalScale;
  };

private:
  std::unique_ptr<CBTree> cbt;

  SubdivisionParams params;
  SubdivisionDisplayParams displayParams;
  etna::Buffer paramsBuffer;

  etna::GraphicsPipeline subdivisionSplitPipeline;
  etna::GraphicsPipeline subdivisionMergePipeline;

  std::unique_ptr<etna::PersistentDescriptorSet> terrainSplitSet;
  std::unique_ptr<etna::PersistentDescriptorSet> terrainMergeSet;

  bool merge;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
