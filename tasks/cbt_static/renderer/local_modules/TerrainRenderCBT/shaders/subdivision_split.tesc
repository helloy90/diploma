#version 460

#extension GL_GOOGLE_include_directive : require

#include "/subdivision/cbt.glsl"
#include "/subdivision/leb.glsl"
#include "SubdivisionParams.h"

struct TerrainInfo
{
  ivec2 extent;
  float heightOffset;
  float heightAmplifier;
};

layout(vertices = 1) out;

layout(location = 0) out triangleData
{
  vec2 texCoord[3];
}
data[];

layout(set = 0, binding = 1) uniform params_t
{
  SubdivisionParams params;
};

layout(set = 1, binding = 0) uniform sampler2D heightMaps[32];
layout(set = 1, binding = 1) readonly buffer infos_t
{
  TerrainInfo infos[];
};


vec4[3] decodeTriangleVertices(CBTNode node)
{
  vec3 xPos = vec3(0, 0, 1);
  vec3 zPos = vec3(1, 0, 0);
  mat2x3 pos = lebSquareNodeDecodeAttribute(node, mat2x3(xPos, zPos));
  vec4 first = params.world * vec4(pos[0][0], 0.0, pos[1][0], 1.0);
  vec4 second = params.world * vec4(pos[0][1], 0.0, pos[1][1], 1.0);
  vec4 third = params.world * vec4(pos[0][2], 0.0, pos[1][2], 1.0);

  for (uint i = 0; i < params.texturesAmount; i++)
  {
    first.y +=
      (texture(heightMaps[i], 0.5 * (first.xz / infos[i].extent) + 0.5).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
    second.y += (texture(heightMaps[i], 0.5 * (second.xz / infos[i].extent) + 0.5).x -
                 infos[i].heightOffset) *
      infos[i].heightAmplifier;
    third.y +=
      (texture(heightMaps[i], 0.5 * (third.xz / infos[i].extent) + 0.5).x - infos[i].heightOffset) *
      infos[i].heightAmplifier;
  }

  return vec4[3](first, second, third);
}

float triangleLOD(vec4[3] triangle_vertices)
{
  vec3 first = (params.view * triangle_vertices[0]).xyz;
  vec3 third = (params.view * triangle_vertices[1]).xyz;

  float squaredLengthSum = dot(first, first) + dot(third, third);
  float bumpToSquare = 2.0 * dot(first, third);

  float distanceToEdgeSqr = squaredLengthSum + bumpToSquare;
  float edgeLengthSqr = squaredLengthSum - bumpToSquare;

  return params.lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
}

bool isVisible(vec4[3] triangle_vertices)
{
  vec3 boxMin =
    min(min(triangle_vertices[0].xyz, triangle_vertices[1].xyz), triangle_vertices[2].xyz);
  vec3 boxMax =
    max(max(triangle_vertices[0].xyz, triangle_vertices[1].xyz), triangle_vertices[2].xyz);

  float a = 1.0;

  for (int i = 0; i < 6 && a >= 0.0; i++)
  {
    bvec3 isInside = greaterThan(params.frustumPlanes[i].xyz, vec3(0));
    vec3 negative = mix(boxMin, boxMax, isInside);

    a = dot(vec4(negative, 1.0), params.frustumPlanes[i]);
  }

  return (a >= 0.0);
}

// bool displacementVariance(vec4[3] triangle_vertices)
// {
//   vec2 first = triangle_vertices[0].xz;
//   vec2 second = triangle_vertices[1].xz;
//   vec2 third = triangle_vertices[2].xz;

//   vec2 middle = (first + second + third) / 3;

//   vec2 dx = (first - second);
//   vec2 dz = (third - second);

//   float variance = 0.0;

//   for (uint i = 0; i < params.texturesAmount; i++)
//   {
//      vec2 displacement = textureGrad(heightMaps[i], middle, dx, dz).xz;
//       variance += clamp(displacement.y - displacement.x * displacement.x, 0.0, 1.0);
//   }

//   return (variance >= params.varianceFactor);
// }

vec2 levelOfDetail(vec4[3] triangle_vertices)
{
  if (!isVisible(triangle_vertices))
  {
    return vec2(0.0, 0.0);
  }

  // if (!displacementVariance(triangle_vertices))
  // {
  //   return vec2(0.0, 1.0);
  // }

  return vec2(triangleLOD(triangle_vertices), 1.0);
}

void main()
{
  CBTNode node = cbtNodeDecode(gl_PrimitiveID);
  vec4 triangleVertices[3] = decodeTriangleVertices(node);

  vec2 targetLOD = levelOfDetail(triangleVertices);

  if (targetLOD.x > 1.0)
  {
    lebSquareNodeSplit(node);
  }

  if (targetLOD.y > 0.0)
  {
    data[gl_InvocationID].texCoord =
      vec2[3](triangleVertices[0].xz, triangleVertices[1].xz, triangleVertices[2].xz);

    gl_TessLevelInner[0] = gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = gl_TessLevelOuter[2] =
      params.tesselationFactor;
  }
  else
  {
    gl_TessLevelInner[0] = 0.0;
    gl_TessLevelInner[1] = 0.0;
    gl_TessLevelOuter[0] = 0.0;
    gl_TessLevelOuter[1] = 0.0;
    gl_TessLevelOuter[2] = 0.0;
  }
}
