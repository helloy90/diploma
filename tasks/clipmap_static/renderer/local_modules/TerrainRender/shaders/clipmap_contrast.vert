#version 460
#extension GL_ARB_separate_shader_objects : enable

struct TerrainInfo
{
  ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};

layout(location = 0) in vec2 vPos;

layout(set = 0, binding = 0) uniform sampler2D heightMaps[32];
layout(set = 0, binding = 1) readonly buffer infos_t
{
  TerrainInfo infos[];
};

layout(std140, set = 1, binding = 0) readonly buffer instance_matrices_t
{
  mat4 instanceMatrices[];
};

layout(set = 1, binding = 1) readonly buffer draw_relems_instance_indices_t
{
  uint drawRelemsInstanceIndices[];
};

layout(push_constant) uniform proj_view_t
{
  mat4 projView;
  uint texturesAmount;
};

layout(location = 0) out VS_OUT
{
  vec3 wPos;
  vec3 normal;
}
vOut;

out gl_PerVertex
{
  vec4 gl_Position;
};

vec3 generateNormal(ivec2 heightMapSize, vec3 pos)
{
  float eps = 1 / float(heightMapSize.x);

  float left = 0;
  float right = 0;
  float up = 0;
  float down = 0;
  for (uint i = 0; i < texturesAmount; i++)
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

void main(void)
{
  mat4 currentModelMatrix = instanceMatrices[drawRelemsInstanceIndices[gl_InstanceIndex]];

  vec3 pos = (currentModelMatrix * vec4(vPos.x, 0, vPos.y, 1.0)).xyz;

  float height = 0;

  for (uint i = 0; i < texturesAmount; i++)
  {
    vec2 texCoord = 0.5 * pos.xz / infos[i].extent + 0.5;
    height +=
      (texture(heightMaps[i], texCoord).x - infos[i].heightOffset) * infos[i].heightAmplifier;
  }

  pos.y = height;

  vOut.wPos = pos;

  ivec2 heightMapSize = textureSize(heightMaps[0], 0);
  vOut.normal = generateNormal(heightMapSize, pos);

  gl_Position = projView * vec4(vOut.wPos, 1.0);
}
