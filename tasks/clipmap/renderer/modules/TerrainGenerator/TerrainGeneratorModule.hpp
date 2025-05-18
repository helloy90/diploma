#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <vector>

#include "etna/BlockingTransferHelper.hpp"
#include "etna/DescriptorSet.hpp"
#include "shaders/TerrainGenerationParams.h"


class TerrainGeneratorModule
{
public:
  struct TerrainCascade
  {
    etna::Image map;
    TerrainGenerationParams params;
    etna::Buffer paramsBuffer;
  };

public:
  TerrainGeneratorModule();
  explicit TerrainGeneratorModule(uint32_t textures_amount);

  void allocateResources(
    vk::Format map_format = vk::Format::eR32Sfloat, vk::Extent3D extent = {1024, 1024, 1});
  void loadShaders();
  void setupPipelines();
  void execute();

  void drawGui();

  // const etna::Image& getMap() const { return terrainMap; }
  std::vector<etna::Binding> getBindings(vk::ImageLayout layout) const;
  const etna::Sampler& getSampler() const { return terrainSampler; }

private:
struct TerrainCascadeInfo {
  glm::ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};

private:
  // etna::Image terrainMap;

  std::vector<TerrainCascade> cascades;

  etna::Sampler terrainSampler;

  // TerrainGenerationParams params;
  // etna::Buffer paramsBuffer;
  std::vector<TerrainCascadeInfo> infos;
  etna::Buffer infosBuffer;

  uint32_t texturesAmount;

  etna::GraphicsPipeline terrainGenerationPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
};
