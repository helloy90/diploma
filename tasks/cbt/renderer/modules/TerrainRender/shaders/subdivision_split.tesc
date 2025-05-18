#version 460

#extension GL_GOOGLE_include_directive : require

#include "/subdivision/cbt.glsl"
#include "/subdivision/leb.glsl"

layout(vertices = 1) out;

layout(location = 0) out triangleData
{
  vec2 texCoord[3];
}
data[];

layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout(push_constant) uniform push_constant_t
{
  mat4 view;
  float lodFactor;
  uint tesselationFactor;
};

vec4[3] decodeTriangleVertices(CBTNode node)
{
  vec3 xPos = vec3(0, 0, 1);
  vec3 yPos = vec3(1, 0, 0);
  mat2x3 pos = lebSquareNodeDecodeAttribute(node, mat2x3(xPos, yPos));
  vec4 first = vec4(pos[0][0], 0.0, pos[1][0], 1.0);
  vec4 second = vec4(pos[0][0], 0.0, pos[1][0], 1.0);
  vec4 third = vec4(pos[0][0], 0.0, pos[1][0], 1.0);

  first.y = texture(heightMap, first.xz).x;
  second.y = texture(heightMap, second.xz).x;
  third.y = texture(heightMap, third.xz).x;

  return vec4[3](first, second, third);
}

float triangleLOD(vec4[3] triangle_vertices)
{
  vec3 first = (view * triangle_vertices[0]).xyz;
  vec3 third = (view * triangle_vertices[2]).xyz;

  vec3 edgeCenter = first + third;
  vec3 edgeVector = third - first;

  float distanceToEdgeSqr = dot(edgeCenter, edgeCenter);
  float edgeLengthSqr = dot(edgeVector, edgeVector);

  return lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
}

vec2 levelOfDetail(vec4[3] triangle_vertices)
{
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

    gl_TessLevelInner[0] = tesselationFactor;
    gl_TessLevelOuter[0] = tesselationFactor;
    gl_TessLevelOuter[1] = tesselationFactor;
    gl_TessLevelOuter[2] = tesselationFactor;
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
