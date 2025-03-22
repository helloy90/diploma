#pragma once

#include <glm/glm.hpp>

#include "resource/Material.hpp"

// Bounds for each render element
struct Bounds
{
  // w coordinate is padding
  glm::vec4 minPos; 
  glm::vec4 maxPos;
};

// A single render element (relem) corresponds to a single draw call
// of a certain pipeline with specific bindings (including material data)
struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;

  Material::Id material = Material::Id::Invalid;

  auto operator<=>(const RenderElement& other) const = default;
};

struct HashRenderElement
{
  std::size_t operator()(const RenderElement& render_element) const
  {
    return std::hash<std::uint32_t>()(render_element.indexCount) ^
      std::hash<std::uint32_t>()(render_element.indexOffset) ^
      std::hash<std::uint32_t>()(render_element.vertexOffset);
  }
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

struct RenderElementGLSLCompat {
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  std::uint32_t material;
};
static_assert(sizeof(RenderElementGLSLCompat) % (sizeof(float) * 4) == 0);

struct MaterialGLSLCompat { 
  glm::vec4 baseColorFactor;
  float roughnessFactor;
  float metallicFactor;
  std::uint32_t baseColorTexture;
  std::uint32_t metallicRoughnessTexture;
  std::uint32_t normalTexture;
  std::uint32_t _padding0 = 0;
  std::uint32_t _padding1 = 0;
  std::uint32_t _padding2 = 0;
};
static_assert(sizeof(MaterialGLSLCompat) % (sizeof(float) * 4) == 0);
