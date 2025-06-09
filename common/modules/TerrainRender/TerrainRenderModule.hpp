#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>

#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/DescriptorSet.hpp>

#include "scene/TerrainManager.hpp"
#include "shaders/MeshesParams.h"
#include "../RenderPacket.hpp"

class TerrainRenderModule
{
public:
  TerrainRenderModule();

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void loadMaps(std::vector<etna::Binding> terrain_bindings);

  void update(const RenderPacket& packet);

  void execute(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params);

  void drawGui();

private:
  void cullTerrain(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const RenderPacket& packet,
    const etna::Buffer& matrices_buffer);

  void renderTerrain(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const RenderPacket& packet,
    const etna::Buffer& matrices_buffer);

private:
  struct PushConstants
  {
    glm::mat4x4 projView;
    uint32_t texturesAmount;
  };

private:
  std::unique_ptr<TerrainManager> terrainMgr;

  MeshesParams meshesParams;

  etna::Buffer meshesParamsBuffer;
  etna::Buffer frustumPlanesBuffer;

  etna::GraphicsPipeline terrainRenderPipeline;
  etna::ComputePipeline cullingPipeline;

  std::unique_ptr<etna::PersistentDescriptorSet> terrainSet;

  uint32_t texturesAmount;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
