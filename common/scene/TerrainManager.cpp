#include "TerrainManager.hpp"

#include <vector>
#include <iostream>

#include <etna/GlobalContext.hpp>


TerrainManager::TerrainManager(uint32_t levels, uint32_t vertex_grid_size)
  : clipmapLevels(levels)
  , vertexGridSize(vertex_grid_size)
  , oneShotCommands{etna::get_context().createOneShotCmdMgr()}
{
  vertexTileSize = (vertex_grid_size + 1) / 4;
  // staging size is overkill, meshes used for clipmap will be smaller
  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = 4 * vertexGridSize * vertexGridSize * sizeof(Vertex)});
}

TerrainManager::ProcessedMeshes TerrainManager::initializeMeshes() const
{
  // this is probably ineffective

  ProcessedMeshes result;

  // uint32_t gridSize = vertexGridSize - 1;
  uint32_t tileSize = vertexTileSize - 1;

  {
    // using 1 square tile, 4 filling meshes between tiles, 1 corner meshes (is rotated when needed)
    // and seam mesh
    std::size_t vertexAmount = vertexTileSize * vertexTileSize + vertexTileSize * 3 * 4 +
      (2 * vertexTileSize + 1) * 2 * 4 /* + ... */;
    result.vertices.reserve(vertexAmount);

    // overkill
    result.indices.reserve(vertexAmount * 6);

    result.relems.reserve(1 + 4 + 1 + 1);
    result.bounds.reserve(1 + 4 + 1 + 1);
    result.meshes.reserve(1 + 1 + 1 + 1);
  }
  std::uint32_t indexValueOffset = 0;
  // square tile
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(1)});

    auto relem = RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = 0};

    for (uint32_t y = 0; y < vertexTileSize; y++)
    {
      for (uint32_t x = 0; x < vertexTileSize; x++)
      {
        auto& vertex = result.vertices.emplace_back();
        vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0); // texcoords are 0 for now
      }
    }

    for (uint32_t y = 0; y < tileSize; y++)
    {
      for (uint32_t x = 0; x < tileSize; x++)
      {
        uint32_t currentIndices[6] = {
          positionToIndexInTile(x, y),
          positionToIndexInTile(x + 1, y + 1),
          positionToIndexInTile(x, y + 1),
          positionToIndexInTile(x, y),
          positionToIndexInTile(x + 1, y),
          positionToIndexInTile(x + 1, y + 1)};

        // without c++23
        for (uint32_t i = 0; i < 6; i++)
        {
          result.indices.emplace_back(currentIndices[i] + indexValueOffset);
        }
      }
    }
    relem.indexCount = tileSize * tileSize * 6;

    spdlog::info(
      "Square mesh - Index count - {} for relem with vertex offset {}, index offset {}",
      relem.indexCount,
      relem.vertexOffset,
      relem.indexOffset);

    result.relems.emplace_back(relem);
    indexValueOffset += positionToIndexInTile(tileSize - 1, tileSize - 1);
  }

  // filler meshes
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(4)});


    uint32_t offset = tileSize;
    {
      // right
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (uint32_t x = 0; x < vertexTileSize; x++)
      {

        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {offset + x + 1, y, 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize; i++)
      {
        uint32_t arm = 0;

        uint32_t bottomLeft = (arm + i) * 2 + 0;
        uint32_t bottomRight = (arm + i) * 2 + 1;
        uint32_t topLeft = (arm + i) * 2 + 2;
        uint32_t topRight = (arm + i) * 2 + 3;

        uint32_t currentIndices[] = {
          bottomLeft, topLeft, topRight, bottomLeft, topRight, bottomRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = tileSize * 6;

      spdlog::info(
        "Filler mesh, right arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);
      indexValueOffset += (tileSize - 1) * 2 + 3;
    }

    {
      // top

      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {x, offset + y + 1, 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize; i++)
      {
        uint32_t arm = 1;

        uint32_t bottomLeft = (arm + i) * 2 + 0;
        uint32_t bottomRight = (arm + i) * 2 + 1;
        uint32_t topLeft = (arm + i) * 2 + 2;
        uint32_t topRight = (arm + i) * 2 + 3;

        // maybe make slightly different later
        uint32_t currentIndices[] = {
          bottomRight, bottomLeft, topLeft, bottomRight, topLeft, topRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = tileSize * 6;

      spdlog::info(
        "Filler mesh, top arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);
      indexValueOffset += (1 + tileSize - 1) * 2 + 3;
    }

    {
      // left
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (uint32_t x = 0; x < vertexTileSize; x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {-int32_t(offset + x), y, 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize; i++)
      {
        uint32_t arm = 2;

        uint32_t bottomLeft = (arm + i) * 2 + 0;
        uint32_t bottomRight = (arm + i) * 2 + 1;
        uint32_t topLeft = (arm + i) * 2 + 2;
        uint32_t topRight = (arm + i) * 2 + 3;

        uint32_t currentIndices[] = {
          bottomLeft, topLeft, topRight, bottomLeft, topRight, bottomRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = tileSize * 6;

      spdlog::info(
        "Filler mesh, left arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);
      indexValueOffset += (2 + tileSize - 1) * 2 + 3;
    }

    {
      // bottom
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {x, -int32_t(offset + y), 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize; i++)
      {
        uint32_t arm = 3;

        uint32_t bottomLeft = (arm + i) * 2 + 0;
        uint32_t bottomRight = (arm + i) * 2 + 1;
        uint32_t topLeft = (arm + i) * 2 + 2;
        uint32_t topRight = (arm + i) * 2 + 3;

        // maybe make slightly different later
        uint32_t currentIndices[] = {
          bottomRight, bottomLeft, topLeft, bottomRight, topLeft, topRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = tileSize * 6;

      spdlog::info(
        "Filler mesh, top arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);
      indexValueOffset += (3 + tileSize - 1) * 2 + 3;
    }
    // for (uint32_t i = 0; i < tileSize * 4; i++)
    // {
    //   uint32_t arm = i / tileSize;

    //   uint32_t bottomLeft = (arm + i) * 2 + 0;
    //   uint32_t bottomRight = (arm + i) * 2 + 1;
    //   uint32_t topLeft = (arm + i) * 2 + 2;
    //   uint32_t topRight = (arm + i) * 2 + 3;

    //   if (arm % 2 == 0) // horizontal arms
    //   {
    //     uint32_t currentIndices[] = {
    //       bottomLeft, topLeft, topRight, bottomLeft, topRight, bottomRight};
    //     for (uint32_t j = 0; j < 6; j++)
    //     {
    //       result.indices.emplace_back(currentIndices[j]);
    //     }
    //   }
    //   else
    //   {
    //     // maybe make slightly different later
    //     uint32_t currentIndices[] = {
    //       bottomRight, bottomLeft, topLeft, bottomRight, topLeft, topRight};
    //     for (uint32_t j = 0; j < 6; j++)
    //     {
    //       result.indices.emplace_back(currentIndices[j]);
    //     }
    //   }
    // }
  }
  // trim mesh
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(1)});

    auto relem = RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = 0};

    glm::vec4 vertexOffset = {-0.5 * (vertexGridSize + 1), -0.5 * (vertexGridSize + 1), 0, 0};

    // vertical
    for (uint32_t y = vertexGridSize; y >= 0; y--)
    {
      for (uint32_t x = 0; x < 2; x++)
      {
        auto& vertex = result.vertices.emplace_back();
        vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0) + vertexOffset;
      }
    }

    uint32_t indexHorizontalOffset = (vertexGridSize + 1) * 2;

    for (uint32_t x = 1; x < vertexGridSize + 1; x++)
    {
      for (uint32_t y = 0; y < 2; y++)
      {
        auto& vertex = result.vertices.emplace_back();
        vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0) + vertexOffset;
      }
    }

    result.relems.emplace_back(relem);
  }

  return result;
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

  // for (auto index : inds)
  // {
  //   std::cout << index << " ";
  // }
  // uploadData(verts, inds);
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
