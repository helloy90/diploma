#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in VS_OUT
{
  vec3 pos;
  vec3 texCoord;
}
surf;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gMaterial;

layout(set = 1, binding = 0) uniform sampler2D heightMap;

vec3 generateNormal(vec2 currentTexCoord, float size)
{
  float eps = 1.0 / size;

  float left = texture(heightMap, currentTexCoord + vec2(-eps, 0)).x;
  float right = texture(heightMap, currentTexCoord + vec2(eps, 0)).x;
  float up = texture(heightMap, currentTexCoord + vec2(0, eps)).x;
  float down = texture(heightMap, currentTexCoord + vec2(0, -eps)).x;

  vec3 normal = normalize(vec3(left - right, 2.0 * eps, down - up));

  return normal;
}

void main()
{
  ivec2 heightMapSize = textureSize(heightMap, 0);
  gAlbedo = vec4(0.5, 0.5, 0.5, 1);
  gNormal = generateNormal(surf.texCoord, float(heightMapSize.x));
  gMaterial = vec4(0.0, 0.8, 0.0, 1.0);
}
