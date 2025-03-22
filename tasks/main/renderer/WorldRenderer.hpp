#pragma once

#include <optional>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>

#include "scene/TerrainManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "shaders/terrain/UniformParams.h"
#include "shaders/terrain/TerrainGenerationParams.h"
#include "GBuffer.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene();
  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupRenderPipelines();
  void rebuildRenderPipelines();
  void setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent);
  void generateTerrain();
  void loadLights(); 

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image);

private:

  void renderTerrain(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void deferredShading(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void updateConstants(etna::Buffer& constants);

private:
  // std::unique_ptr<SceneManager> sceneMgr;
  std::unique_ptr<TerrainManager> terrainMgr;

  vk::Format renderTargetFormat;

  etna::Image mainViewDepth;

  etna::Image terrainMap;
  etna::Image terrainNormalMap;
  std::optional<etna::GpuSharedResource<etna::Buffer>> generationParamsBuffer;
  TerrainGenerationParams generationParams;
  uint32_t maxNumberOfSamples;

  etna::Image renderTarget;

  std::optional<GBuffer> gBuffer;
  etna::Buffer lightsBuffer;
  etna::Buffer directionalLightsBuffer;

  UniformParams params;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;

  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::GraphicsPipeline terrainRenderPipeline;
  etna::GraphicsPipeline deferredShadingPipeline;

  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramInfoBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> distributionBuffer;

  std::uint32_t binsAmount;

  etna::ComputePipeline terrainNormalPipeline;
  etna::ComputePipeline lightDisplacementPipeline;

  etna::Sampler terrainSampler;

  bool wireframeEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 resolution;
};
