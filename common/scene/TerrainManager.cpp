#include "TerrainManager.hpp"

#include <vector>

#include <glm/fwd.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <tracy/Tracy.hpp>
#include <fmt/std.h>

#include <etna/GlobalContext.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "RenderStructs.hpp"


#define DEBUG_FILE_WRITE 0

#if DEBUG_FILE_WRITE

static auto logger = spdlog::basic_logger_mt("file_logger", "logs.txt");

#endif

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

  uint32_t gridSize = vertexGridSize - 1;
  uint32_t tileSize = vertexTileSize - 1;

  // for debugging purposes
  int32_t currentMaxIndex = -1;
  int32_t currentOffsetAddition = 0;

  {
    // using 1 cross mesh, 1 square tile, 4 filling meshes between tiles, 1 trim mesh (is rotated
    // when needed) and (not initialized for now) seam mesh
    std::size_t vertexAmount = (2 * vertexTileSize + 1) * 2 + vertexTileSize * vertexTileSize +
      vertexTileSize * 3 * 4 + (2 * vertexGridSize + 1) * 2 + 4 * vertexGridSize;
    result.vertices.reserve(vertexAmount);

    // overkill
    result.indices.reserve(vertexAmount * 6);

    // 2 for cross, 1 for square, 4 for fillers, 2 for trim, 1 for seam
    std::size_t relemsAmount = 2 + 1 + 4 + 2 + 1;
    result.relems.reserve(relemsAmount);
    result.bounds.reserve(relemsAmount);
    result.meshes.reserve(1 + 1 + 1 + 1 + 1);
  }

  // cross mesh
  {
    result.meshes.push_back(
      Mesh{
        .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
        .relemCount = static_cast<std::uint32_t>(2)});

    // horizontal
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = (tileSize * 2 + 1) * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif

      for (uint32_t y = 0; y < 2; y++)
      {
        for (int32_t x = -static_cast<int32_t>(tileSize);
             x < static_cast<int32_t>(vertexTileSize + 1);
             x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, y};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = {-static_cast<int32_t>(tileSize), 0},
          .maxPos = {static_cast<int32_t>(vertexTileSize), 1}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t x = 0; x < tileSize * 2 + 1; x++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(x, 0, 2 * vertexTileSize),
          positionToIndex(x + 1, 1, 2 * vertexTileSize),
          positionToIndex(x, 1, 2 * vertexTileSize),
          positionToIndex(x, 0, 2 * vertexTileSize),
          positionToIndex(x + 1, 0, 2 * vertexTileSize),
          positionToIndex(x + 1, 1, 2 * vertexTileSize)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Cross mesh, horizonal segment - Index count - {} for relem with vertex offset {}, index "
          "offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(tileSize * 2 + 1, 1, 2 * vertexTileSize) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum index for "
        "current mesh + 1- "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }

    // vertical
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = (tileSize * 2 + 1) * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (int32_t y = -static_cast<int32_t>(tileSize);
           y < static_cast<int32_t>(vertexTileSize + 1);
           y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, y};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = {0, -static_cast<int32_t>(tileSize)},
          .maxPos = {1, static_cast<int32_t>(vertexTileSize)}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t y = 0; y < tileSize * 2 + 1; y++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(0, y, 2),
          positionToIndex(1, y + 1, 2),
          positionToIndex(0, y + 1, 2),
          positionToIndex(0, y, 2),
          positionToIndex(1, y, 2),
          positionToIndex(1, y + 1, 2)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Cross mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
          "offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(1, tileSize * 2 + 1, 2) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum index for "
        "current mesh + 1- "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }
  }

  // square tile
  {
    result.meshes.push_back(
      Mesh{
        .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
        .relemCount = static_cast<std::uint32_t>(1)});

    auto relem = (RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = tileSize * tileSize * 6});

#if DEBUG_FILE_WRITE
    {
      logger->info("\nVertices:");
    }
#endif
    for (uint32_t y = 0; y < vertexTileSize; y++)
    {
      for (uint32_t x = 0; x < vertexTileSize; x++)
      {
        auto& vertex = result.vertices.emplace_back();
        vertex.position = {x, y};

#if DEBUG_FILE_WRITE
        {
          logger->info(
            "\tvertex {}, position - {}",
            result.vertices.size() - 1,
            glm::to_string(vertex.position));
        }
#endif
      }
    }

    result.bounds.emplace_back(
      Bounds{.minPos = {0, 0}, .maxPos = {vertexTileSize - 1, vertexTileSize - 1}});

    for (uint32_t y = 0; y < tileSize; y++)
    {
      for (uint32_t x = 0; x < tileSize; x++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(x, y, vertexTileSize),
          positionToIndex(x + 1, y + 1, vertexTileSize),
          positionToIndex(x, y + 1, vertexTileSize),
          positionToIndex(x, y, vertexTileSize),
          positionToIndex(x + 1, y, vertexTileSize),
          positionToIndex(x + 1, y + 1, vertexTileSize)};

        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }
    }

#if DEBUG_FILE_WRITE
    {
      logger->info(
        "Square mesh - Index count - {} for relem with vertex offset {}, index offset {}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);
    }
#endif
    result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
    {
      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));
    }
#endif
    currentOffsetAddition = positionToIndex(tileSize, tileSize, vertexTileSize) + 1;
    ETNA_VERIFYF(
      currentOffsetAddition == currentMaxIndex + 1,
      "Wrong index offset will be added! Maximum index for current "
      "mesh + 1 - "
      "{}, supposed offset - {}",
      currentMaxIndex + 1,
      currentOffsetAddition);

    currentMaxIndex = -1;
  }

  if (clipmapLevels == 0)
  {
    return result;
  }

  // filler meshes
  {
    result.meshes.push_back(
      Mesh{
        .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
        .relemCount = static_cast<std::uint32_t>(4)});

    uint32_t offset = tileSize;
    {
      // right
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (uint32_t y = 0; y < 2; y++)
      {
        for (uint32_t x = 0; x < vertexTileSize; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {offset + x + 1, y};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(Bounds{.minPos = {0, 0}, .maxPos = {offset + vertexTileSize, 1}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t x = 0; x < tileSize; x++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(x, 0, vertexTileSize),
          positionToIndex(x + 1, 1, vertexTileSize),
          positionToIndex(x, 1, vertexTileSize),
          positionToIndex(x, 0, vertexTileSize),
          positionToIndex(x + 1, 0, vertexTileSize),
          positionToIndex(x + 1, 1, vertexTileSize)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Filler mesh, right arm - Index count - {} for relem with vertex offset {}, index offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(tileSize, 1, vertexTileSize) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum index for "
        "current mesh + 1 - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }

    {
      // top
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, offset + y + 1};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(Bounds{.minPos = {0, 0}, .maxPos = {1, offset + vertexTileSize}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t y = 0; y < tileSize; y++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(0, y, 2),
          positionToIndex(1, y + 1, 2),
          positionToIndex(0, y + 1, 2),
          positionToIndex(0, y, 2),
          positionToIndex(1, y, 2),
          positionToIndex(1, y + 1, 2)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Filler mesh, top arm - Index count - {} for relem with vertex offset {}, index offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(1, tileSize, 2) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum "
        "index for "
        "current mesh + 1 - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }

    {
      // left
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (uint32_t y = 0; y < 2; y++)
      {
        for (uint32_t x = 0; x < vertexTileSize; x++)
        {

          auto& vertex = result.vertices.emplace_back();
          vertex.position = {-int32_t(offset + x), y};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = {-int32_t(offset + vertexTileSize - 1), 0},
          .maxPos = {-int32_t(offset + 0), 1}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t x = 0; x < tileSize; x++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(x + 1, 0, vertexTileSize),
          positionToIndex(x, 1, vertexTileSize),
          positionToIndex(x + 1, 1, vertexTileSize),
          positionToIndex(x + 1, 0, vertexTileSize),
          positionToIndex(x, 0, vertexTileSize),
          positionToIndex(x, 1, vertexTileSize)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Filler mesh, left arm - Index count - {} for relem with vertex offset {}, index offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(tileSize, 1, vertexTileSize) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }

    {
      // bottom
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = tileSize * 6});

#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (uint32_t y = 0; y < vertexTileSize; y++)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = {x, -int32_t(offset + y)};

#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = {0, -int32_t(offset + vertexTileSize - 1)},
          .maxPos = {1, -int32_t(offset + 0)}});

#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t y = 0; y < tileSize; y++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(0, y + 1, 2),
          positionToIndex(1, y, 2),
          positionToIndex(0, y, 2),
          positionToIndex(0, y + 1, 2),
          positionToIndex(1, y + 1, 2),
          positionToIndex(1, y, 2)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);

#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Filler mesh, bottom arm - Index count - {} for relem with vertex offset {}, index "
          "offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(1, tileSize, 2) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum "
        "index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }
  }

  // trim mesh
  {
    result.meshes.push_back(
      Mesh{
        .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
        .relemCount = static_cast<std::uint32_t>(2)});

    glm::vec2 vertexOffset = glm::vec2(-static_cast<glm::float32>(vertexGridSize) * 0.5f);
    // vertical
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 6 * (gridSize)});
#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (int32_t y = vertexGridSize - 1; y >= 0; y--)
      {
        for (uint32_t x = 0; x < 2; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = glm::vec2(x, y) + vertexOffset;
#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = glm::vec2(0, 0) + vertexOffset,
          .maxPos = glm::vec2(1, vertexGridSize - 1) + vertexOffset});
#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t y = 0; y < gridSize; y++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(0, y + 1, 2),
          positionToIndex(1, y, 2),
          positionToIndex(0, y, 2),
          positionToIndex(0, y + 1, 2),
          positionToIndex(1, y + 1, 2),
          positionToIndex(1, y, 2)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);
#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Trim mesh, vertical segment - Index count - {} for relem with vertex offset {}, index "
          "offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);

#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition =
        positionToIndex(1, gridSize, 2) + 1; // because connected in the same mesh
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum index for current "
        "mesh - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }

    // horizontal
    {
      auto relem = (RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = 6 * (gridSize - 1)});
#if DEBUG_FILE_WRITE
      {
        logger->info("\nVertices:");
      }
#endif
      for (uint32_t y = 0; y < 2; y++)
      {
        for (uint32_t x = 1; x < vertexGridSize; x++)
        {
          auto& vertex = result.vertices.emplace_back();
          vertex.position = glm::vec2(x, y) + vertexOffset;
#if DEBUG_FILE_WRITE
          {
            logger->info(
              "\tvertex {}, position - {}",
              result.vertices.size() - 1,
              glm::to_string(vertex.position));
          }
#endif
        }
      }

      result.bounds.emplace_back(
        Bounds{
          .minPos = glm::vec2(1, 0) + vertexOffset,
          .maxPos = glm::vec2(vertexGridSize - 1, 1) + vertexOffset});
#if DEBUG_FILE_WRITE
      {
        logger->info("Indices:");
      }
#endif
      for (uint32_t x = 0; x < gridSize - 1; x++)
      {
        uint32_t currentIndices[] = {
          positionToIndex(x, 0, vertexGridSize - 1),
          positionToIndex(x + 1, 1, vertexGridSize - 1),
          positionToIndex(x, 1, vertexGridSize - 1),
          positionToIndex(x, 0, vertexGridSize - 1),
          positionToIndex(x + 1, 0, vertexGridSize - 1),
          positionToIndex(x + 1, 1, vertexGridSize - 1)};
        for (auto index : currentIndices)
        {
          currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
          result.indices.emplace_back(index);
#if DEBUG_FILE_WRITE
          {
            logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
          }
#endif
        }
      }
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Trim mesh, horizontal segment - Index count - {} for relem with vertex offset {}, "
          "index "
          "offset "
          "{}",
          relem.indexCount,
          relem.vertexOffset,
          relem.indexOffset);
      }
#endif
      result.relems.emplace_back(relem);
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "Bounds of this relem - min: {}, max: {}",
          glm::to_string(result.bounds.back().minPos),
          glm::to_string(result.bounds.back().maxPos));
      }
#endif
      currentOffsetAddition = positionToIndex(gridSize - 1, 1, vertexGridSize - 1) + 1;
      ETNA_VERIFYF(
        currentOffsetAddition == currentMaxIndex + 1,
        "Wrong index offset will be added! Maximum index for current "
        "mesh + 1 - "
        "{}, supposed offset - {}",
        currentMaxIndex + 1,
        currentOffsetAddition);
      currentMaxIndex = -1;
    }
  }

  // seam
  {
    result.meshes.push_back(
      Mesh{
        .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
        .relemCount = static_cast<std::uint32_t>(1)});

    auto relem = (RenderElement{
      .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
      .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
      .indexCount = (vertexGridSize * 4 - 4) * 3 / 2});
#if DEBUG_FILE_WRITE
    {
      logger->info("\nVertices:");
    }
#endif
    for (uint32_t i = 0; i < vertexGridSize - 1; i++)
    {
      auto& vertex = result.vertices.emplace_back();
      vertex.position = glm::vec2(i, 0);
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "\tvertex {}, position - {}",
          result.vertices.size() - 1,
          glm::to_string(vertex.position));
      }
#endif
    }
    for (uint32_t i = 0; i < vertexGridSize - 1; i++)
    {
      auto& vertex = result.vertices.emplace_back();
      vertex.position = glm::vec2(vertexGridSize - 1, i);
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "\tvertex {}, position - {}",
          result.vertices.size() - 1,
          glm::to_string(vertex.position));
      }
#endif
    }
    for (uint32_t i = 0; i < vertexGridSize - 1; i++)
    {
      auto& vertex = result.vertices.emplace_back();
      vertex.position = glm::vec2(vertexGridSize - 1 - i, vertexGridSize - 1);
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "\tvertex {}, position - {}",
          result.vertices.size() - 1,
          glm::to_string(vertex.position));
      }
#endif
    }
    for (uint32_t i = 0; i < vertexGridSize - 1; i++)
    {
      auto& vertex = result.vertices.emplace_back();
      vertex.position = glm::vec2(0, vertexGridSize - 1 - i);
#if DEBUG_FILE_WRITE
      {
        logger->info(
          "\tvertex {}, position - {}",
          result.vertices.size() - 1,
          glm::to_string(vertex.position));
      }
#endif
    }

    result.bounds.emplace_back(
      Bounds{.minPos = {0, 0}, .maxPos = {vertexGridSize, vertexGridSize}});
#if DEBUG_FILE_WRITE
    {
      logger->info("Indices:");
    }
#endif
    for (uint32_t i = 0; i < vertexGridSize * 4 - 4; i += 2)
    {
      uint32_t currentIndices[] = {i + 1, i, i + 2};
      for (auto index : currentIndices)
      {
        currentMaxIndex = glm::max(currentMaxIndex, static_cast<int32_t>(index));
        result.indices.emplace_back(index);
#if DEBUG_FILE_WRITE
        {
          logger->info("index {}, value - {}", result.indices.size() - 1, result.indices.back());
        }
#endif
      }
    }
    result.indices.back() = 0;
#if DEBUG_FILE_WRITE
    {
      logger->info(
        "Seam mesh, Index count - {} for relem with vertex offset {}, index "
        "offset "
        "{}",
        relem.indexCount,
        relem.vertexOffset,
        relem.indexOffset);
    }
#endif
    result.relems.emplace_back(relem);
#if DEBUG_FILE_WRITE
    {
      logger->info(
        "Bounds of this relem - min: {}, max: {}",
        glm::to_string(result.bounds.back().minPos),
        glm::to_string(result.bounds.back().maxPos));
    }
#endif
    currentOffsetAddition = vertexGridSize * 4 - 4 + 1;
    ETNA_VERIFYF(
      currentOffsetAddition == currentMaxIndex + 1,
      "Wrong index offset will be added! Maximum index for current "
      "mesh + 1 - "
      "{}, supposed offset - {}",
      currentMaxIndex + 1,
      currentOffsetAddition);
    currentMaxIndex = -1;
  }


  for (uint32_t i = 0; i < result.meshes.size(); i++)
  {
    auto& mesh = result.meshes[i];
    spdlog::info(
      "Mesh {}: first relem - {}, relem count - {}", i, mesh.firstRelem, mesh.relemCount);
    for (uint32_t j = mesh.firstRelem; j < mesh.firstRelem + mesh.relemCount; j++)
    {
      auto& currentRelem = result.relems[j];
      spdlog::info(
        "\tRelem {}: v - {}, i - {}, i_c - {}",
        j,
        currentRelem.vertexOffset,
        currentRelem.indexOffset,
        currentRelem.indexCount);
    }
  }

  return result;
}

TerrainManager::ProcessedInstances TerrainManager::processInstances() const
{
  ProcessedInstances result;

  std::size_t instancesAmount =
    1 + 4 + 12 * clipmapLevels + clipmapLevels + clipmapLevels + clipmapLevels;

  result.matrices.reserve(instancesAmount);
  result.meshes.reserve(instancesAmount);

  std::uint32_t crossMesh = 0;
  std::uint32_t squareMesh = 1;
  std::uint32_t fillerMesh = 2;
  std::uint32_t trimMesh = 3;
  std::uint32_t seamMesh = 4;

  const auto& identityMat = glm::identity<glm::mat4x4>();

  // cross and 4 inner squares
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
    scale = 1u << level;
    for (uint32_t i = 0; i < 12; i++)
    {
      result.matrices.emplace_back(glm::scale(identityMat, {scale, 1, scale}));
      result.meshes.emplace_back(squareMesh);
    }
  }

  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1u << level;
    result.matrices.emplace_back(glm::scale(identityMat, {scale, 1, scale}));
    result.meshes.emplace_back(fillerMesh);
  }

  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1u << level;
    result.matrices.emplace_back(glm::scale(identityMat, {scale, 1, scale}));
    result.meshes.emplace_back(trimMesh);
  }

  for (uint32_t level = 0; level < clipmapLevels; level++)
  {
    scale = 1u << level;
    result.matrices.emplace_back(glm::scale(identityMat, {scale, 1, scale}));
    result.meshes.emplace_back(seamMesh);
  }

  spdlog::info("instances array size - {}", result.meshes.size());
  return result;
}

void TerrainManager::uploadData(
  std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
  auto& ctx = etna::get_context();

  unifiedVbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = vertices.size_bytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedTerrainVbuf",
    });

  unifiedIbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = indices.size_bytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedTerrainIbuf",
    });

  transferHelper->uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper->uploadBuffer<std::uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);

  unifiedRelemsbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = renderElements.size() * sizeof(RenderElementGLSLCompat),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedRelemsbuf"});

  // maybe unnesessary
  std::vector<RenderElementGLSLCompat> renderElementsData;
  renderElementsData.reserve(renderElements.size());
  for (const auto& relem : renderElements)
  {
    renderElementsData.emplace_back(
      RenderElementGLSLCompat{
        .vertexOffset = relem.vertexOffset,
        .indexOffset = relem.indexOffset,
        .indexCount = relem.indexCount});
  }

  transferHelper->uploadBuffer<RenderElementGLSLCompat>(
    *oneShotCommands, unifiedRelemsbuf, 0, std::span(renderElementsData));

  unifiedBoundsbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = renderElementsBounds.size() * sizeof(Bounds),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedBoundsbuf"});
  unifiedMeshesbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = meshes.size() * sizeof(Mesh),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedMeshesbuf"});

  unifiedInstanceMatricesbuf.emplace(
    ctx.getMainWorkCount(),
    [&ctx, instanceMatricesSize = this->instanceMatrices.size()](std::size_t i) {
      return ctx.createBuffer(
        etna::Buffer::CreateInfo{
          .size = instanceMatricesSize * sizeof(glm::mat4x4),
          .bufferUsage =
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
          .memoryUsage = VMA_MEMORY_USAGE_AUTO,
          .allocationCreate = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
          .name = fmt::format("unifiedInstanceMatricesbuf{}", i)});
    });

  spdlog::info(
    "{} - relem bounds size, {} - instance matrices size",
    renderElementsBounds.size(),
    instanceMatrices.size());

  unifiedInstanceMeshesbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = instanceMeshes.size() * sizeof(std::uint32_t),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedInstanceMeshesbuf"});

  transferHelper->uploadBuffer<Bounds>(
    *oneShotCommands, unifiedBoundsbuf, 0, std::span(renderElementsBounds));
  transferHelper->uploadBuffer<Mesh>(*oneShotCommands, unifiedMeshesbuf, 0, std::span(meshes));

  transferHelper->uploadBuffer<std::uint32_t>(
    *oneShotCommands, unifiedInstanceMeshesbuf, 0, std::span(instanceMeshes));

  std::size_t drawRelemsInstancesIndicesSize = 0;
  for (auto meshIndex : instanceMeshes)
  {
    drawRelemsInstancesIndicesSize += meshes[meshIndex].relemCount;
  }

  // filled on GPU when culling
  unifiedDrawRelemsInstanceIndicesbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = drawRelemsInstancesIndicesSize * sizeof(std::uint32_t),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedDrawRelemsInstanceIndicesbuf"});

  unifiedRelemInstanceOffsetsbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = renderElements.size() * sizeof(std::uint32_t),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
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

  transferHelper->uploadBuffer<std::uint32_t>(
    *oneShotCommands, unifiedRelemInstanceOffsetsbuf, 0, std::span(relemInstanceOffsets));

  unifiedDrawCommandsbuf = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = renderElements.size() * sizeof(vk::DrawIndexedIndirectCommand),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = "unifiedDrawCommandsbuf"});

  std::vector<vk::DrawIndexedIndirectCommand> drawCommands;
  drawCommands.reserve(renderElements.size());
  for (uint32_t i = 0; i < renderElements.size(); i++)
  {
    drawCommands.emplace_back(
      vk::DrawIndexedIndirectCommand{
        .indexCount = renderElements[i].indexCount,
        .instanceCount = 0,
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
  ZoneScopedN("moveClipmap");
  glm::vec2 cameraHorizontalPosition = glm::vec2(camera_position.x, camera_position.z);

  std::uint32_t meshOffset = 0;

  std::size_t tileSize = vertexTileSize - 1;

  std::size_t crossMesh = 0;
  std::size_t squareMesh = 1;
  std::size_t fillerMesh = 2;
  std::size_t trimMesh = 3;
  std::size_t seamMesh = 4;

  glm::vec2 scale = {};
  glm::vec2 snappedPosition = {};

  glm::vec2 tileExtent = {};
  glm::vec2 base = {};

  glm::vec2 newPosition = {};

  // cross
  {
    snappedPosition = glm::floor(cameraHorizontalPosition);

    instanceMatrices[meshOffset][3].x = snappedPosition.x;
    instanceMatrices[meshOffset][3].y = 0;
    instanceMatrices[meshOffset][3].z = snappedPosition.y;

    ETNA_VERIFYF(
      instanceMeshes[meshOffset] == crossMesh,
      "Displacing wrong model, current - {}, needed - {}",
      instanceMeshes[meshOffset],
      crossMesh);

    meshOffset++;
  }

  // square tiles
  {
    glm::vec2 fillerSkip = {};
    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(static_cast<float>(1u << level));
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      tileExtent = glm::vec2(static_cast<float>(tileSize << level));
      base = snappedPosition - glm::vec2(static_cast<float>((tileSize) << (level + 1)));

      for (uint32_t x = 0; x < 4; x++)
      {
        for (uint32_t z = 0; z < 4; z++)
        {
          if (level != 0 && (x == 1 || x == 2) && (z == 1 || z == 2))
          {
            continue;
          }

          fillerSkip = glm::vec2(x < 2 ? 0 : 1, z < 2 ? 0 : 1) * scale;

          newPosition = base + glm::vec2(x, z) * tileExtent + fillerSkip;

          instanceMatrices[meshOffset][3].x = newPosition.x;
          instanceMatrices[meshOffset][3].y = 0;
          instanceMatrices[meshOffset][3].z = newPosition.y;

          ETNA_VERIFYF(
            instanceMeshes[meshOffset] == squareMesh,
            "Displacing wrong model, current - {}, needed - {}",
            instanceMeshes[meshOffset],
            squareMesh);

          meshOffset++;
        }
      }
    }
  }

  if (clipmapLevels == 0)
  {
    auto& currentInstanceMatricesBuffer = unifiedInstanceMatricesbuf->get();
    currentInstanceMatricesBuffer.map();
    std::memcpy(
      currentInstanceMatricesBuffer.data(),
      instanceMatrices.data(),
      instanceMatrices.size() * sizeof(glm::mat4x4));
    currentInstanceMatricesBuffer.unmap();

    return;
  }

  // filler meshes
  {
    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(static_cast<float>(1u << level));
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      newPosition = snappedPosition;

      instanceMatrices[meshOffset][3].x = newPosition.x;
      instanceMatrices[meshOffset][3].y = 0;
      instanceMatrices[meshOffset][3].z = newPosition.y;

      ETNA_VERIFYF(
        instanceMeshes[meshOffset] == fillerMesh,
        "Displacing wrong model, current - {}, needed - {}",
        instanceMeshes[meshOffset],
        fillerMesh);

      meshOffset++;
    }
  }

  glm::vec2 nextScale = {};
  glm::vec2 nextSnappedPosition = {};
  // trim meshes
  {
    glm::vec2 tileCenter = {};
    glm::vec2 diff = {};

    // 00 - 0 degrees (0), 01 - 90 degrees(1), 10 - 270 degrees (2), 11 - 180 degrees(3)
    glm::mat4x4 rotationMatrices[] = {
      glm::identity<glm::mat4x4>(),
      glm::mat4x4(0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1),
      glm::mat4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1),
      glm::mat4x4(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1)};

    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(static_cast<float>(1u << level));
      snappedPosition = glm::floor(cameraHorizontalPosition / scale) * scale;

      nextScale = glm::vec2(static_cast<float>(1u << (level + 1)));
      nextSnappedPosition = glm::floor(cameraHorizontalPosition / nextScale) * nextScale;

      tileCenter = snappedPosition + scale * glm::vec2(0.5);

      diff = cameraHorizontalPosition - nextSnappedPosition;

      uint32_t index = 0;
      index |= (diff.x < scale.x ? 2 : 0);
      index |= (diff.y < scale.y ? 1 : 0);

      newPosition = tileCenter;

      instanceMatrices[meshOffset] = rotationMatrices[index] *
        glm::scale(glm::identity<glm::mat4x4>(), glm::vec3(scale.x, 0, scale.x));

      instanceMatrices[meshOffset][3].x = newPosition.x;
      instanceMatrices[meshOffset][3].y = 0;
      instanceMatrices[meshOffset][3].z = newPosition.y;

      ETNA_VERIFYF(
        instanceMeshes[meshOffset] == trimMesh,
        "Displacing wrong model, current - {}, needed - {}",
        instanceMeshes[meshOffset],
        trimMesh);

      meshOffset++;
    }
  }

  // seam meshes
  {
    glm::vec2 nextBase = {};

    for (uint32_t level = 0; level < clipmapLevels; level++)
    {
      scale = glm::vec2(static_cast<float>(1u << level));
      nextScale = glm::vec2(static_cast<float>(1u << (level + 1)));
      nextSnappedPosition = glm::floor(cameraHorizontalPosition / nextScale) * nextScale;

      nextBase = nextSnappedPosition - glm::vec2(static_cast<float>((tileSize) << (level + 1)));

      newPosition = nextBase;

      instanceMatrices[meshOffset][3].x = newPosition.x;
      instanceMatrices[meshOffset][3].y = 0;
      instanceMatrices[meshOffset][3].z = newPosition.y;

      ETNA_VERIFYF(
        instanceMeshes[meshOffset] == seamMesh,
        "Displacing wrong model, current - {}, needed - {}",
        instanceMeshes[meshOffset],
        seamMesh);

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
