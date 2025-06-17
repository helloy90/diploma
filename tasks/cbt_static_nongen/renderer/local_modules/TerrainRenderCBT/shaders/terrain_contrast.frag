#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "SubdivisionParams.h"

layout(location = 0) in VS_OUT
{
  vec4 pos;
  vec2 texCoord;
  vec3 normal;
}
surf;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gMaterial;

struct TerrainInfo
{
  ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};

layout(set = 0, binding = 1) uniform params_t
{
  SubdivisionParams params;
};

layout(set = 1, binding = 0) uniform sampler2D heightMaps[32];
layout(set = 1, binding = 1) readonly buffer infos_t
{
  TerrainInfo infos[];
};

void main()
{
  gAlbedo = vec4(0.5, 0.5, 0.5, 1);
  gNormal = surf.normal;
  gMaterial = vec4(0.0, 0.8, 0.0, 1.0);
}
