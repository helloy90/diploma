#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../terrain/UniformParams.h"

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec2 texCoord;
} surf;

void main()
{
  gAlbedo = vec4(0.5, 0.5, 0.5, 1);
  gNormal = surf.wNorm;
  gMaterial = vec4(0.0, 0.6, 0.05, 1.0);
}