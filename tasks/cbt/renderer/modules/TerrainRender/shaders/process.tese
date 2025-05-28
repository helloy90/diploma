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

    // return vec2(factor.x) * tex_coords[0] + vec2(factor.y) * tex_coords[1] + vec2(factor.z) * tex_coords[2];
}

Attributes tesselateTriangle(vec2 tex_coords[3], vec3 factor)
{
  vec2 texCoord = interpolate(tex_coords, factor);
  vec4 position = vec4(texCoord.x, 0, texCoord.y, 1);

  for (uint i = 0; i < params.texturesAmount; i++)
  {
    position.y +=
      (texture(heightMaps[i], texCoord).x - infos[i].heightOffset) * infos[i].heightAmplifier;
  }

  return Attributes(position, texCoord);
}

void main()
{
  Attributes attributes = tesselateTriangle(data[0].texCoords, gl_TessCoord.xyz);

  pos = params.world * attributes.position;
  texCoord = attributes.texCoord;

  gl_Position = params.worldProjView * attributes.position;
}
