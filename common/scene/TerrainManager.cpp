#include "TerrainManager.hpp"
#include "etna/Assert.hpp"

#include <cstdint>
#include <fmt/core.h>
#include <glm/fwd.hpp>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <etna/GlobalContext.hpp>
#include <vulkan/vulkan_enums.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "RenderStructs.hpp"


static auto logger = spdlog::basic_logger_mt("file_logger", "logs.txt");


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
    // needed), cross mesh and (not initialized for now) seam mesh
    std::size_t vertexAmount = vertexTileSize * vertexTileSize + vertexTileSize * 2 * 4 +
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
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = (tileSize * 2 + 1) * 6});

      logger->info("Vertices:");
      for (int32_t x = -static_cast<int32_t>(tileSize);
           x < static_cast<int32_t>(vertexTileSize + 1);
           x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, y};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(Bounds{
        .minPos = {-static_cast<int32_t>(tileSize), 0, 0, 0},
        .maxPos = {static_cast<int32_t>(vertexTileSize), 1, 0, 0}});

      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Cross mesh, horizonal segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }

    // vertical
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = (tileSize * 2 + 1) * 6});

      logger->info("Vertices:");
      for (int32_t y = -static_cast<int32_t>(tileSize);
           y < static_cast<int32_t>(vertexTileSize + 1);
           y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, y};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(Bounds{
        .minPos = {0, -static_cast<int32_t>(tileSize), 0, 0},
        .maxPos = {1, static_cast<int32_t>(vertexTileSize), 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Cross mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }
  }

  // square tile
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(1)});

    auto relem = (RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = tileSize * tileSize * 6});

    logger->info("Vertices:");
    for (uint32_t y = 0; y < vertexTileSize; y++)
    {
      for (uint32_t x = 0; x < vertexTileSize; x++)
      {
        auto& vertex = result.vertices.emplace_back();
        vertex.position = {x, y};
        logger->info(
          "\tvertex {}, position - {}",
          result.vertices.size() - 1,
          glm::to_string(vertex.position));
      }
    }

    result.bounds.emplace_back(
      Bounds{.minPos = {0, 0, 0, 0}, .maxPos = {vertexTileSize - 1, vertexTileSize - 1, 0, 0}});

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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }
    }

    logger->info(
      "Square mesh - Index count - {} for relem with vertex offset {}, index offset {}",
      relem.indexCount,
      relem.vertexOffset,
      relem.indexOffset);

    result.relems.emplace_back(relem);

    logger->info(
      "Bounds of this relem - min: {}, max: {}",
      glm::to_string(result.bounds.back().minPos),
      glm::to_string(result.bounds.back().maxPos));

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
    indexValueOffset += currentOffsetAddition;
    logger->info("indexValueOffset now is {}", indexValueOffset);
    indexValueOffset = 0;
  }

  // filler meshes
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(4)});

    auto relem = (RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = tileSize * 6});

    uint32_t offset = tileSize;
    {
      // right
      logger->info("Vertices:");
      for (uint32_t x = 0; x < vertexTileSize; x++)
      {

        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {offset + x + 1, y};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(
        Bounds{.minPos = {0, 0, 0, 0}, .maxPos = {vertexTileSize - 1, 1, 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Filler mesh, right arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }

    {
      // top
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});
      logger->info("Vertices:");
      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, offset + y + 1};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(
        Bounds{.minPos = {0, 0, 0, 0}, .maxPos = {1, vertexTileSize - 1, 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Filler mesh, top arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }

    {
      // left
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});
      logger->info("Vertices:");
      for (uint32_t x = 0; x < vertexTileSize; x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {-int32_t(offset + x), y};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(Bounds{
        .minPos = {-int32_t(offset + 0), 0, 0, 0},
        .maxPos = {-int32_t(offset + vertexTileSize - 1), 1, 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Filler mesh, left arm - Index count - {} for relem with vertex offset {}, index offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

      currentOffsetAddition = (2 + tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from tom arm relem of filler mesh + 1 "
        "- "
        "{}, maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }

    {
      // bottom
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});
      logger->info("Vertices:");
      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, -int32_t(offset + y)};
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(Bounds{
        .minPos = {0, -int32_t(offset + 0), 0, 0},
        .maxPos = {1, -int32_t(offset + vertexTileSize - 1), 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Filler mesh, bottom arm - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

      currentOffsetAddition = (3 + tileSize - 1) * 2 + 3 + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Current offset from left arm relem of filler mesh + 1 "
        "- "
        "{}, maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        indexValueOffset,
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
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
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 6 * (vertexGridSize + 1)});
      logger->info("Vertices:");
      for (int32_t y = vertexGridSize; y >= 0; y--)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = glm::vec4(x, y, 0, 0) + vertexOffset;
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(
        Bounds{.minPos = {0, 0, 0, 0}, .maxPos = {1, vertexGridSize, 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Trim mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }

    // horizontal
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 6 * (vertexGridSize - 1)});
      logger->info("Vertices:");
      for (uint32_t x = 1; x < vertexGridSize + 1; x++)
      {
        for (uint32_t y = 0; y < 2; y++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = glm::vec4(x, y, 0, 0) + vertexOffset;
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
      }

      result.bounds.emplace_back(
        Bounds{.minPos = {1, 0, 0, 0}, .maxPos = {vertexGridSize, 1, 0, 0}});
      logger->info("Indices:");
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
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
      }

      logger->info(
        "Trim mesh, horizontal segment - Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);

      result.relems.emplace_back(relem);

      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));

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

      indexValueOffset += currentOffsetAddition;
      logger->info("indexValueOffset now is {}", indexValueOffset);
      indexValueOffset = 0;
    }
  }

  // seam
  {
  }


  for (uint32_t i = 0; i < result.meshes.size(); i++)
  {
    auto& mesh = result.meshes[i];
    spdlog::info(
      "Mesh {}: first relem - {}, relem count - {}", i, mesh.firstRelem, mesh.relemCount);
    for (uint32_t j = mesh.firstRelem; j < mesh.firstRelem + mesh.relemCount; j++)
    {
      auto& currentrelem = result.relems[j];
      spdlog::info(
        "\tRelem {}: v - {}, i - {}, i_c - {}",
        j,
        currentrelem.vertexOffset,
        currentrelem.indexOffset,
        currentrelem.indexCount);
    }
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

  const auto& identityMat = glm::identity<glm::mat4x4>();

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

  spdlog::info("instances array size - {}", result.meshes.size());
  return result;
}

void TerrainManager::uploadData(
  std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
  auto& ctx = etna::get_context();

  unifiedVbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = vertices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedTerrainVbuf",
  });

  unifiedIbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = indices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedTerrainIbuf",
  });

  transferHelper->uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper->uploadBuffer<std::uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);

  unifiedRelemsbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = renderElements.size() * sizeof(RenderElementGLSLCompat),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedRelemsbuf"});

  // maybe unnesessary
  std::vector<RenderElementGLSLCompat> renderElementsData;
  renderElementsData.reserve(renderElements.size());
  for (const auto& relem : renderElements)
  {
    renderElementsData.emplace_back(RenderElementGLSLCompat{
      .vertexOffset = relem.vertexOffset,
      .indexOffset = relem.indexOffset,
      .indexCount = relem.indexCount});
  }

  transferHelper->uploadBuffer<RenderElementGLSLCompat>(
    *oneShotCommands, unifiedRelemsbuf, 0, std::span(renderElementsData));

  unifiedBoundsbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = renderElementsBounds.size() * sizeof(Bounds),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedBoundsbuf"});
  unifiedMeshesbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = meshes.size() * sizeof(Mesh),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedMeshesbuf"});

  unifiedInstanceMatricesbuf.emplace(
    ctx.getMainWorkCount(),
    [&ctx, instanceMatricesSize = this->instanceMatrices.size()](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = instanceMatricesSize * sizeof(glm::mat4x4),
        .bufferUsage =
          vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO,
        .allocationCreate =
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .name = fmt::format("unifiedInstanceMatricesbuf{}", i)});
    });

  unifiedInstanceMeshesbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = instanceMeshes.size() * sizeof(std::uint32_t),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedInstanceMeshesbuf"});

  transferHelper->uploadBuffer<Bounds>(
    *oneShotCommands, unifiedBoundsbuf, 0, std::span(renderElementsBounds));
  transferHelper->uploadBuffer<Mesh>(*oneShotCommands, unifiedMeshesbuf, 0, std::span(meshes));

  transferHelper->uploadBuffer<std::uint32_t>(
    *oneShotCommands, unifiedInstanceMeshesbuf, 0, std::span(instanceMeshes));

  // filled on GPU when culling
  unifiedDrawInstanceIndicesbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = instanceMeshes.size() * sizeof(std::uint32_t),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedDrawInstanceIndicesbuf"});

  unifiedRelemInstanceOffsetsbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = renderElements.size() * sizeof(std::uint32_t),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedRelemInstanceOffsetsbuf"});

  std::vector<std::uint32_t> relemInstanceOffsets(renderElements.size(), 0);
  for (const auto& meshIdx : instanceMeshes)
  {
    const auto& currentMesh = meshes[meshIdx];
    for (std::uint32_t relemIdx = currentMesh.firstRelem;
         relemIdx < currentMesh.firstRelem + currentMesh.relemCount;
         relemIdx++)
    {
      relemInstanceOffsets[relemIdx]++;
    }
  }

  std::uint32_t offset = 0;
  std::uint32_t previousAmount = 0;
  for (auto& amount : relemInstanceOffsets)
  {
    previousAmount = amount;
    amount = offset;
    offset += previousAmount;
  }

  for (uint32_t i = 0; i < relemInstanceOffsets.size(); i++)
  {
    spdlog::info("relem instance {} offset - {}", i, relemInstanceOffsets[i]);
  }

  transferHelper->uploadBuffer<std::uint32_t>(
    *oneShotCommands, unifiedRelemInstanceOffsetsbuf, 0, std::span(relemInstanceOffsets));

  unifiedDrawCommandsbuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = renderElements.size() * sizeof(vk::DrawIndexedIndirectCommand),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndirectBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = "unifiedDrawCommandsbuf"});

  std::vector<vk::DrawIndexedIndirectCommand> drawCommands;
  drawCommands.reserve(renderElements.size());
  std::vector<uint32_t> counts = {1, 1, 64, 5, 5, 5, 5, 5, 5};
  for (uint32_t i = 0; i < renderElements.size(); i++)
  {
    drawCommands.emplace_back(vk::DrawIndexedIndirectCommand{
      .indexCount = renderElements[i].indexCount,
      .instanceCount = counts[i],
      .firstIndex = renderElements[i].indexOffset,
      .vertexOffset = static_cast<std::int32_t>(renderElements[i].vertexOffset),
      .firstInstance = relemInstanceOffsets[i]});
  }

  transferHelper->uploadBuffer<vk::DrawIndexedIndirectCommand>(
    *oneShotCommands, unifiedDrawCommandsbuf, 0, std::span(drawCommands));
}

void TerrainManager::loadTerrain()
{
  auto [verts, inds, relems, meshs, bounds] = initializeMeshes();

  renderElements = std::move(relems);
  meshes = std::move(meshs);
  renderElementsBounds = std::move(bounds);

  auto [instMats, instMeshes] = processInstances();
  instanceMatrices = std::move(instMats);
  instanceMeshes = std::move(instMeshes);

  uploadData(verts, inds);

  spdlog::info("vertices amount - {}, indices amount - {}", verts.size(), inds.size());
}

void TerrainManager::moveClipmap(glm::vec3 camera_position)
{
  ZoneScoped;
  glm::vec2 cameraHorizontalPosition = glm::vec2(camera_position.x, camera_position.z);
  glm::vec2 snappedPosition = glm::floor(cameraHorizontalPosition);

  std::uint32_t meshOffset = 0;
  // cross
  {
    instanceMatrices[meshOffset][3].x = snappedPosition.x;
    instanceMatrices[meshOffset][3].y = 0;
    instanceMatrices[meshOffset][3].z = snappedPosition.y;

    meshOffset++;
  }

  glm::vec2 scale = glm::vec2(1);
  glm::vec2 tileSize = glm::vec2(vertexTileSize - 1);
  glm::vec2 base = snappedPosition - glm::vec2((vertexTileSize - 1));
  glm::vec2 newPosition = {};
  // 4 inner squares
  {
    for (uint32_t x = 0; x < 2; x++)
    {
      for (uint32_t z = 0; z < 2; z++)
      {
        newPosition = base + glm::vec2(x, z) * tileSize;

        instanceMatrices[meshOffset][3].x = newPosition.x;
        instanceMatrices[meshOffset][3].y = 0;
        instanceMatrices[meshOffset][3].z = newPosition.y;

        meshOffset++;
      }
    }
  }

  // square tiles
  {
    glm::vec2 fillerSkip = {};
    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(1 << level);
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      tileSize = glm::vec2((vertexTileSize - 1) << level);
      base = snappedPosition - glm::vec2((vertexTileSize - 1) << (level + 1));

      for (uint32_t x = 0; x < 4; x++)
      {
        for (uint32_t z = 0; z < 4; z++)
        {
          if ((x == 1 || x == 2) && (z == 1 || z == 2))
          {
            continue;
          }

          fillerSkip = glm::vec2(x < 2 ? 0 : 1, z < 2 ? 0 : 1) * scale;

          newPosition = base + glm::vec2(x, z) * tileSize + fillerSkip;

          instanceMatrices[meshOffset][3].x = newPosition.x;
          instanceMatrices[meshOffset][3].y = 0;
          instanceMatrices[meshOffset][3].z = newPosition.y;

          ETNA_VERIFYF(
            instanceMeshes[meshOffset] == 1,
            "Displacing wrong model, current - {}, needed - {}",
            instanceMeshes[meshOffset],
            1);

          meshOffset++;
        }
      }
    }
  }

  // filler meshes
  {
    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(1 << level);
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      newPosition = snappedPosition;

      instanceMatrices[meshOffset][3].x = newPosition.x;
      instanceMatrices[meshOffset][3].y = 0;
      instanceMatrices[meshOffset][3].z = newPosition.y;

      ETNA_VERIFYF(
        instanceMeshes[meshOffset] == 2,
        "Displacing wrong model, current - {}, needed - {}",
        instanceMeshes[meshOffset],
        2);

      meshOffset++;
    }
  }

  // trim meshes
  {
    glm::vec2 nextScale = {};
    glm::vec2 nextSnappedPosition = {};

    glm::vec2 tileCenter = {};
    glm::vec2 diff = {};

    // 00 - 0 degrees, 10 - 90 degrees, 01 - 270 degrees, 11 - 180 degrees
    float rotationAngles[] = {0, 270.0f, 90.0f, 180.0f};

    for (uint32_t level = 0; level < clipmapLevels - 1; level++)
    {
      scale = glm::vec2(1 << level);
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      nextScale = scale * glm::vec2(2);
      nextSnappedPosition = glm::floor(cameraHorizontalPosition / nextScale) * nextScale;

      tileCenter = snappedPosition + scale * glm::vec2(0.5);
      diff = snappedPosition - nextSnappedPosition;

      uint32_t rotation = 0;
      // scale.x == scale.y
      rotation |= (diff.x < scale.x ? 2 : 0);
      rotation |= (diff.y < scale.y ? 1 : 0);

      newPosition = tileCenter;

      instanceMatrices[meshOffset] = glm::translate(
        glm::rotate(
          glm::scale(glm::identity<glm::mat4x4>(), glm::vec3(scale.x, scale.x, scale.x)),
          rotationAngles[rotation],
          glm::vec3(0, 1, 0)),
        glm::vec3(newPosition.x, 0, newPosition.y));

      ETNA_VERIFYF(
        instanceMeshes[meshOffset] == 3,
        "Displacing wrong model, current - {}, needed - {}",
        instanceMeshes[meshOffset],
        3);

      meshOffset++;
    }
  }

  auto& currentInstanceMatricesBuffer = unifiedInstanceMatricesbuf->get();
  currentInstanceMatricesBuffer.map();
  std::memcpy(
    currentInstanceMatricesBuffer.data(),
    instanceMatrices.data(),
    instanceMatrices.size() * sizeof(glm::mat4x4));
  currentInstanceMatricesBuffer.unmap();
}

etna::VertexByteStreamFormatDescription TerrainManager::getVertexFormatDescription()
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32Sfloat,
        .offset = 0,
      },
    }};
}
