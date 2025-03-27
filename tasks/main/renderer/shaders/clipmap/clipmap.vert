#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../terrain/UniformParams.h"

layout (location = 0) in vec2 vPos;

layout (std140, binding = 0) readonly buffer instance_matrices_t {
  mat4 instanceMatrices[];
};

layout (binding = 1) readonly buffer draw_relems_instance_indices_t {
  uint drawRelemsInstanceIndices[];
};

layout (binding = 2) uniform params_t {
  UniformParams params;
};

layout (binding = 3) uniform sampler2D heightMap;

layout (location = 0) out VS_OUT
{
  vec3 wPos;
  vec2 texCoord;
} vOut;

out gl_PerVertex { vec4 gl_Position; };


void main(void) {
  mat4 currentModelMatrix = instanceMatrices[drawRelemsInstanceIndices[gl_InstanceIndex]];

  vOut.texCoord = 0.5 * (vPos.xy / params.extent) + 0.5;

  float height = texture(heightMap, vOut.texCoord).x;

  vOut.wPos = (currentModelMatrix * vec4(vPos.x, (height - params.heightOffset * 0.9) * params.heightAmplifier, vPos.y, 1.0)).xyz;

  gl_Position = params.projView * vec4(vOut.wPos, 1.0);
}