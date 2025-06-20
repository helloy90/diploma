#pragma once

#include <memory>
#include <optional>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>

#include "wsi/Keyboard.hpp"

#include "modules/Light/LightModule.hpp"
#include "modules/TerrainGenerator/TerrainGeneratorModule.hpp"
#include "local_modules/TerrainRender/TerrainRenderModule.hpp"
#include "modules/RenderPacket.hpp"

#include "FramePacket.hpp"

#include "shaders/UniformParams.h"
#include "GBuffer.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void loadShaders();
  void setupRenderPipelines();
  void rebuildRenderPipelines();

  void loadCubemap();

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image);

private:
  void deferredShading(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

private:
  LightModule lightModule;
  TerrainGeneratorModule terrainGeneratorModule;
  TerrainRenderModule terrainRenderModule;

  bool freezeClipmap;

  vk::Format renderTargetFormat;

  etna::Image cubemapTexture;
  
  etna::Image renderTarget;

  std::optional<GBuffer> gBuffer;

  UniformParams params;
  RenderPacket renderPacket;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;

  etna::GraphicsPipeline deferredShadingPipeline;

  etna::Sampler cubemapSampler;

  bool wireframeEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 resolution;
};
