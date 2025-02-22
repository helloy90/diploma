#include "TerrainManager.hpp"

#include <cstdint>
#include <etna/GlobalContext.hpp>
#include <glm/fwd.hpp>
#include <vector>


TerrainManager::TerrainManager(uint32_t levels, uint32_t vertex_grid_size)
  : clipmapLevels(levels)
  , vertexGridSize(vertex_grid_size)
  , oneShotCommands{etna::get_context().createOneShotCmdMgr()}
{
  vertexTileSize = (vertex_grid_size + 1) / 4;
  // staging size is overkill, meshes used for clipmap will be smaller
  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = vertexGridSize * vertexGridSize * sizeof(Vertex)});
}

TerrainManager::ProcessedMeshes TerrainManager::initializeMeshes() const
{
  // this is probably ineffective

  ProcessedMeshes result;

  // uint32_t gridSize = vertexGridSize - 1;
  // uint32_t tileSize = vertexTileSize - 1;

  {
    //using 1 square tile, 4 filling meshes between tiles, 4 corner meshes (so no rotation needed)
    std::size_t vertexAmount = vertexTileSize * vertexTileSize + vertexTileSize * 3 * 4 + (2 * vertexTileSize + 1) * 2 * 4;
    result.vertices.reserve(vertexAmount);
    result.indices.reserve(vertexAmount * 6);
  }

  for (uint32_t y = 0; y < vertexTileSize; y++) {
    for (uint32_t x = 0; x < vertexTileSize; x++) {
      auto& vertex = result.vertices.emplace_back();
      vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0); //texcoords are 0 for now
    }
  }

  for (uint32_t y = 0; y < vertexTileSize; y++) {
    for (uint32_t x = 0; x < vertexTileSize; x++) {
      // uint32_t quadIndex = 0;
      // auto& index = result.indices.emplace_back();
    }
  }
  return {};
}

TerrainManager::ProcessedInstances TerrainManager::processInstances() const
{
  return {};
}

void TerrainManager::uploadData(
  std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
  unifiedVbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = vertices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedTerrainVbuf",
  });

  unifiedIbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = indices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedTerrainIbuf",
  });

  transferHelper->uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper->uploadBuffer<std::uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);
}

void TerrainManager::loadTerrain()
{
  auto [instMats, instMeshes] = processInstances();
  instanceMatrices = std::move(instMats);
  instanceMeshes = std::move(instMeshes);

  auto [verts, inds, relems, meshs, bounds] = initializeMeshes();

  renderElements = std::move(relems);
  meshes = std::move(meshs);
  renderElementsBounds = std::move(bounds);

  uploadData(verts, inds);
}

etna::VertexByteStreamFormatDescription TerrainManager::getVertexFormatDescription()
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
      },
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = sizeof(glm::vec4),
      },
    }};
}

