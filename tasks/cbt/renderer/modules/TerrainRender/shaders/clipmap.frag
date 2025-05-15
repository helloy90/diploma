#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gMaterial;

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec2 texCoord;
}
surf;

layout(binding = 4) uniform sampler2D heightMap;

const vec2 fidelity = vec2(8, 8);

vec3 generateNormal(vec2 currentTexCoord, ivec2 heightMapSize) {
  float eps = float(fidelity.x) / float(heightMapSize.x);

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
  gNormal = generateNormal(surf.texCoord, heightMapSize);
  gMaterial = vec4(0.0, 0.6, 0.0, 1.0);
}
