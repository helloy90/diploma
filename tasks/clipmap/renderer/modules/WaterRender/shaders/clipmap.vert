#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "WaterParams.h"

layout(location = 0) in vec2 vPos;

layout(std140, binding = 0) readonly buffer instance_matrices_t
{
  mat4 instanceMatrices[];
};

layout(binding = 1) readonly buffer draw_relems_instance_indices_t
{
  uint drawRelemsInstanceIndices[];
};

layout(binding = 2) uniform terrain_params_t
{
  WaterParams params;
};

layout(binding = 4) uniform sampler2D heightMap;

layout(push_constant) uniform proj_view_t
{
  mat4 projView;
  vec4 cameraWorldPosition;
};

layout(location = 0) out VS_OUT
{
  vec3 wPos;
  vec2 texCoord;
}
vOut;

out gl_PerVertex
{
  vec4 gl_Position;
};


void main(void)
{
  mat4 currentModelMatrix = instanceMatrices[drawRelemsInstanceIndices[gl_InstanceIndex]];

  vec3 pos = (currentModelMatrix * vec4(vPos.x, 0, vPos.y, 1.0)).xyz;

  vOut.texCoord = 0.5 * (pos.xz / params.extent) + 0.5;
  float height = texture(heightMap, vOut.texCoord).x;

  pos.y = height;

  vOut.wPos = pos;

  gl_Position = projView * vec4(vOut.wPos, 1.0);
}
