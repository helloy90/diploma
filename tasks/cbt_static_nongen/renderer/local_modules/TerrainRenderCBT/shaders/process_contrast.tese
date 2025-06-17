#version 460
#extension GL_GOOGLE_include_directive : require

#include "SubdivisionParams.h"

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in triangleData
{
  vec2 texCoords[3];
}
data[];

layout(location = 0) out VS_OUT
{
  vec4 pos;
  vec2 texCoord;
  vec3 normal;
};

struct TerrainInfo
{
  ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};

struct Attributes
{
  vec4 position;
  vec2 texCoord;
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

vec2 interpolate(vec2 tex_coords[3], vec3 factor)
{
  return tex_coords[1] + factor.x * (tex_coords[2] - tex_coords[1]) +
    factor.y * (tex_coords[0] - tex_coords[1]);
}

Attributes tesselateTriangle(vec2 tex_coords[3], vec3 factor)
{
  vec2 texCoord = interpolate(tex_coords, factor);
  vec4 position = vec4(texCoord.x, 0, texCoord.y, 1);

  for (uint i = 0; i < params.texturesAmount; i++)
  {
    position.y +=
      (texture(heightMaps[i], 0.5 * (texCoord / infos[i].extent) + 0.5).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
  }

  return Attributes(position, texCoord);
}

vec3 generateNormal(ivec2 heightMapSize, vec3 pos)
{
  float eps = 1 / float(heightMapSize.x);

  float left = 0;
  float right = 0;
  float up = 0;
  float down = 0;
  for (uint i = 0; i < params.texturesAmount; i++)
  {
    vec2 texCoord = 0.5 * (pos.xz) / infos[i].extent + 0.5;
    left += (texture(heightMaps[i], texCoord + vec2(-eps, 0)).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
    right += (texture(heightMaps[i], texCoord + vec2(eps, 0)).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
    up += (texture(heightMaps[i], texCoord + vec2(0, eps)).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
    down += (texture(heightMaps[i], texCoord + vec2(0, -eps)).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
  }

  vec3 normal = normalize(vec3(left - right, 2.0 * eps, down - up));

  return normal;
}

void main()
{
  Attributes attributes = tesselateTriangle(data[0].texCoords, gl_TessCoord.xyz);

  pos = attributes.position;
  texCoord = attributes.texCoord;
  ivec2 heightMapSize = textureSize(heightMaps[0], 0);
  normal = generateNormal(heightMapSize, pos.xyz);

  gl_Position = params.projView * attributes.position;
}
