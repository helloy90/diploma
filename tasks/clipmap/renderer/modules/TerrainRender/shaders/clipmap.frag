#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gMaterial;

struct TerrainInfo
{
  ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};


layout(location = 0) in VS_OUT
{
  vec3 wPos;
}
surf;

layout(set = 0, binding = 0) uniform sampler2D heightMaps[32];
layout(set = 0, binding = 1) readonly buffer infos_t
{
  TerrainInfo infos[];
};

layout(push_constant) uniform proj_view_t
{
  mat4 projView;
  uint texturesAmount;
};

vec3 generateNormal(ivec2 heightMapSize)
{
  float eps = 1 / float(infos[texturesAmount - 1].extent);

  float left = 0;
  float right = 0;
  float up = 0;
  float down = 0;
  for (uint i = 0; i < 1; i++)
  {
    left += texture(heightMaps[i], 0.5 * (surf.wPos.xz) / infos[i].extent + 0.5 + vec2(-eps, 0)).x;
    right += texture(heightMaps[i], 0.5 * (surf.wPos.xz) / infos[i].extent + 0.5 + vec2(eps, 0)).x;
    up += texture(heightMaps[i], 0.5 * (surf.wPos.xz) / infos[i].extent + 0.5 + vec2(0, eps)).x;
    down += texture(heightMaps[i], 0.5 * (surf.wPos.xz) / infos[i].extent + 0.5 + vec2(0, -eps)).x;
  }

  vec3 normal = normalize(vec3(left - right, 2.0 * eps, down - up));

  return normal;
}

void main()
{
  ivec2 heightMapSize = textureSize(heightMaps[0], 0);
  gAlbedo = vec4(0.5, 0.5, 0.5, 1);
  gNormal = generateNormal(heightMapSize);
  gMaterial = vec4(0.0, 1.0, 0.0, 1.0);
}
