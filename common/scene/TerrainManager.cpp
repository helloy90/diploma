#include "TerrainManager.hpp"

#include <vector>

#include <glm/ext/matrix_transform.hpp>

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

  // for debugging purposes
  int32_t currentMaxIndex = -1;
  int32_t currentOffsetAddition = 0;

  {
    // using 1 square tile, 4 filling meshes between tiles, 1 corner meshes (is rotated when
    // needed), cross mesh and seam mesh
    std::size_t vertexAmount = vertexTileSize * vertexTileSize + vertexTileSize * 3 * 4 +
      (2 * vertexTileSize + 1) * 2 * 4 /* + ... */;
    result.vertices.reserve(vertexAmount);

    // overkill
    result.indices.reserve(vertexAmount * 6);

    std::size_t relemsAmount = 2 + 1 + 4 + 2 + 1;
    result.relems.reserve(relemsAmount);
    result.bounds.reserve(relemsAmount);
    result.meshes.reserve(1 + 1 + 1 + 1 + 1);
  }
  std::uint32_t indexValueOffset = 0;

  // cross mesh
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(2)});

    // horizontal
    {
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (int32_t x = -static_cast<int32_t>(tileSize);
           x < static_cast<int32_t>(vertexTileSize + 1);
           x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {x, y, 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize * 2 + 1; i++)
      {
        uint32_t bottomLeft = i * 2 + 0;
        uint32_t bottomRight = i * 2 + 1;
        uint32_t topLeft = i * 2 + 2;
        uint32_t topRight = i * 2 + 3;

        uint32_t currentIndices[] = {
          bottomRight, bottomLeft, topRight, bottomLeft, topLeft, topRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = (tileSize * 2 + 1) * 6;

      spdlog::info(
        "Cross mesh, horizonal segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      currentOffsetAddition = tileSize * 2 * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from start - {}, maximum index for "
        "current mesh + 1- "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
    }

    // vertical
    {
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (int32_t y = -static_cast<int32_t>(tileSize);
           y < static_cast<int32_t>(vertexTileSize + 1);
           y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = {x, y, 0, 0};
        }
      }

      for (uint32_t i = 0; i < tileSize * 2 + 1; i++)
      {
        uint32_t bottomLeft = i * 2 + 0;
        uint32_t bottomRight = i * 2 + 1;
        uint32_t topLeft = i * 2 + 2;
        uint32_t topRight = i * 2 + 3;

        uint32_t currentIndices[] = {
          bottomRight, topRight, bottomLeft, bottomLeft, topRight, topLeft};
        for (uint32_t j = 0; j < 6; j++)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = (tileSize * 2 + 1) * 6;

      spdlog::info(
        "Cross mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      currentOffsetAddition = tileSize * 2 * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from horizontal element of cross mesh - "
        "{}, maximum index for "
        "current mesh + 1- "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
    }
  }

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
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[i]));
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

    currentOffsetAddition = positionToIndexInTile(tileSize, tileSize) + 1;
    ETNA_VERIFYF(
      currentOffsetAddition == currentMaxIndex + 1,
      "Wrong index offset will be added! Current offset from vertical arm of cross mesh - {}, "
      "maximum index for current "
      "mesh + 1 - "
      "{}, supposed offset - {}",
      indexValueOffset,
      currentMaxIndex + 1,
      currentOffsetAddition);
    currentMaxIndex = -1;

    result.relems.emplace_back(relem);
    indexValueOffset += currentOffsetAddition;
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
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
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

      currentOffsetAddition = (tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from square mesh - {}, maximum index for "
        "current mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
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
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
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

      currentOffsetAddition = (1 + tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from right arm of filler mesh relem + 1  "
        "- {}, maximum "
        "index for "
        "current mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
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
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
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

      currentOffsetAddition = (2 + tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from tom arm relem of filler mesh + 1 - "
        "{}, maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
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
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = tileSize * 6;

      spdlog::info(
        "Filler mesh, bottom arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      currentOffsetAddition = (3 + tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from left arm relem of filler mesh + 1 - "
        "{}, maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
    }
  }

  // trim mesh
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(2)});

    glm::vec4 vertexOffset = {-0.5 * (vertexGridSize + 1), -0.5 * (vertexGridSize + 1), 0, 0};
    // vertical
    {
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (int32_t y = vertexGridSize; y >= 0; y--)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0) + vertexOffset;
        }
      }

      for (uint32_t i = 0; i < vertexGridSize; i++)
      {
        uint32_t bottomLeft = (i + 1) * 2 + 0;
        uint32_t bottomRight = (i + 1) * 2 + 1;
        uint32_t topLeft = (i + 0) * 2 + 0;
        uint32_t topRight = (i + 0) * 2 + 1;
        uint32_t currentIndices[] = {
          topRight, topLeft, bottomLeft, bottomRight, topRight, bottomLeft};
        for (uint32_t j = 0; j < 6; j++)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = 6 * (vertexGridSize + 1);

      spdlog::info(
        "Trim mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      currentOffsetAddition = vertexGridSize * 2 + 1; // because connected in the same mesh
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex,
        "Wrong index offset will be added! Current offset from bottom arm relem of filler mesh "
        "- {}, maximum index for current "
        "mesh - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
    }

    // horizontal
    {
      auto relem = RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 0};

      for (uint32_t x = 1; x < vertexGridSize + 1; x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.positionAndTexcoord = glm::vec4(x, y, 0, 0) + vertexOffset;
        }
      }

      for (uint32_t i = 0; i < vertexGridSize - 1; i++)
      {
        uint32_t bottomLeft = (i + 0) * 2 + 0;
        uint32_t bottomRight = (i + 1) * 2 + 0;
        uint32_t topLeft = (i + 0) * 2 + 1;
        uint32_t topRight = (i + 1) * 2 + 1;
        uint32_t currentIndices[] = {
          topLeft, bottomLeft, bottomRight, topRight, topLeft, bottomRight};
        for (uint32_t j = 0; j < 6; j++)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(currentIndices[j]));
          result.indices.emplace_back(currentIndices[j] + indexValueOffset);
        }
      }

      relem.indexCount = 6 * (vertexGridSize - 1);

      spdlog::info(
        "Trim mesh, horizontal segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      currentOffsetAddition = (vertexGridSize - 1) * 2 + 1 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from vertical arm relem of trim mesh - "
        "{}, maximum index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      result.relems.emplace_back(relem);
      indexValueOffset += currentOffsetAddition;
    }
  }

  // seam
  {
  }

  return result;
}

TerrainManager::ProcessedInstances TerrainManager::processInstances() const
{
  ProcessedInstances result;

  std::size_t instancesAmount = 1 + 4 + 12 * clipmapLevels + clipmapLevels + clipmapLevels;

  result.matrices.reserve(instancesAmount);
  result.meshes.reserve(instancesAmount);

  std::size_t crossMesh = 0;
  std::size_t squareMesh = 1;
  std::size_t fillerMesh = 2;
  std::size_t trimMesh = 3;
  // std::size_t seamMesh = 4;

  const auto identityMat = glm::identity<glm::mat4x4>();

  // cross mesh and 4 inner squares
  {
    result.matrices.emplace_back(identityMat);
    result.meshes.emplace_back(crossMesh);

    for (uint32_t x = 0; x < 2; x++)
    {
      for (uint32_t y = 0; y < 2; y++)
      {
        result.matrices.emplace_back(identityMat);
        result.meshes.emplace_back(squareMesh);
      }
    }
  }

  uint32_t scale = 1;
  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1 << level;
    for (uint32_t i = 0; i < 12; i++)
    {
      result.matrices.emplace_back(glm::scale(identityMat, {scale, scale, scale}));
      result.meshes.emplace_back(squareMesh);
    }
  }

  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1 << level;
    result.matrices.emplace_back(glm::scale(identityMat, {scale, scale, scale}));
    result.meshes.emplace_back(fillerMesh);
  }

  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1 << level;
    result.matrices.emplace_back(glm::scale(identityMat, {scale, scale, scale}));
    result.meshes.emplace_back(trimMesh);
  }

  return result;
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

  // uploadData(verts, inds);
}

void TerrainManager::moveClipmap(glm::vec3) {}

etna::VertexByteStreamFormatDescription TerrainManager::getVertexFormatDescription()
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
      },
    }};
}
