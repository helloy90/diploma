#version 460
#extension GL_GOOGLE_include_directive : require

#include "SubdivisionParams.h"
#include "WaterParams.h"


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

struct Attributes
{
  vec4 position;
  vec2 texCoord;
};

layout(binding = 1) uniform params_t
{
  SubdivisionParams subdivisionParams;
};

layout(binding = 2) uniform terrain_params_t
{
  WaterParams waterParams;
};

layout(binding = 4) uniform sampler2D heightMap;

vec2 interpolate(vec2 tex_coords[3], vec3 factor)
{
  return tex_coords[1] + factor.x * (tex_coords[2] - tex_coords[1]) +
    factor.y * (tex_coords[0] - tex_coords[1]);
}

Attributes tesselateTriangle(vec2 tex_coords[3], vec3 factor)
{
  vec2 texCoord = interpolate(tex_coords, factor);
  vec4 position = vec4(texCoord.x, 0, texCoord.y, 1);

  position.y = texture(heightMap, 0.5 * (texCoord / waterParams.extent) + 0.5).x;

  return Attributes(position, texCoord);
}

void main()
{
  Attributes attributes = tesselateTriangle(data[0].texCoords, gl_TessCoord.xyz);

  pos = attributes.position;
  texCoord = attributes.texCoord / waterParams.extent;

  gl_Position = subdivisionParams.projView * attributes.position;
}
