#pragma once

#include <span>

#include <etna/Buffer.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/VertexInput.hpp>
#include <etna/GpuSharedResource.hpp>

#include "RenderStructs.hpp"


class TerrainManager
{
public:
  TerrainManager(uint32_t levels, uint32_t vertex_grid_size);

  void loadTerrain();

  void moveClipmap(glm::vec3 camera_position);

  // Every instance is a mesh drawn with a certain transform
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const std::uint32_t> getInstanceMeshes() { return instanceMeshes; }

  // Every mesh is a collection of relems
  std::span<const Mesh> getMeshes() { return meshes; }

  // Every relem is a single draw call
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  std::span<const Bounds> getRenderElementsBounds() { return renderElementsBounds; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::Buffer& getRelemsBuffer() { return unifiedRelemsbuf; }
  etna::Buffer& getBoundsBuffer() { return unifiedBoundsbuf; }
  etna::Buffer& getMeshesBuffer() { return unifiedMeshesbuf; }
  etna::Buffer& getInstanceMeshesBuffer() { return unifiedInstanceMeshesbuf; }
  etna::Buffer& getInstanceMatricesBuffer() { return unifiedInstanceMatricesbuf->get(); }
  etna::Buffer& getRelemInstanceOffsetsBuffer() { return unifiedRelemInstanceOffsetsbuf; }
  etna::Buffer& getDrawInstanceIndicesBuffer() { return unifiedDrawInstanceIndicesbuf; }
  etna::Buffer& getDrawCommandsBuffer() { return unifiedDrawCommandsbuf; }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

private:
  struct Vertex
  {
    // First 2 floats are position, last 2 float are texcoords
    // normals computed in shader
    glm::vec2 position;
    // glm::vec2 texCoord;
  };

  // static_assert(sizeof(Vertex) == sizeof(float) * 4);

  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<std::uint32_t> meshes;
  };

  struct ProcessedMeshes
  {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
    std::vector<Bounds> bounds;
  };

  uint32_t positionToIndexInTile(uint32_t x, uint32_t y) const { return y * vertexTileSize + x; };

  ProcessedInstances processInstances() const;
  ProcessedMeshes initializeMeshes() const;
  void uploadData(std::span<const Vertex> vertices, std::span<const std::uint32_t>);

private:
  uint32_t clipmapLevels;
  uint32_t vertexGridSize; // should be 2^k - 1
  uint32_t vertexTileSize; // almost always (vertexGridSize + 1) / 4

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<std::uint32_t> instanceMeshes;
  std::vector<Bounds> renderElementsBounds;

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  etna::Buffer unifiedRelemsbuf;
  etna::Buffer unifiedBoundsbuf;
  etna::Buffer unifiedMeshesbuf;

  std::optional<etna::GpuSharedResource<etna::Buffer>> unifiedInstanceMatricesbuf;

  etna::Buffer unifiedInstanceMeshesbuf;
  etna::Buffer unifiedRelemInstanceOffsetsbuf;

  etna::Buffer unifiedDrawInstanceIndicesbuf;

  etna::Buffer unifiedDrawCommandsbuf;
};
