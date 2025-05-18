#version 460

layout(triangles, ccw, equal_spacing) in;

layout(location = 0) in triangleData
{
  vec2 texCoords[3];
}
data[];

layout(location = 0) out VS_OUT
{
  vec3 pos;
  vec2 texCoord;
};

layout(set = 1, binding = 0) uniform sampler2D heightMap;


struct Attributes
{
  vec4 position;
  vec2 texCoord;
};

vec2 interpolate(vec2 tex_coords[3], vec2 factor)
{
  return tex_coords[1] + factor.x * (tex_coords[2] - tex_coords[1]) +
    factor.y * (tex_coords[0] - tex_coords[1]);
}

Attributes tesselateTriangle(vec2 tex_coords[3], vec2 factor)
{
  vec2 texCoord = interpolate(tex_coords, factor);
  vec4 position = vec4(texCoord.x, 0, texCoord.y, 1);

  position.y = texture(heightMap, texCoord).x;

  return Attributes(position, texCoord);
}

void main()
{
  Attributes attributes = tesselateTriangle(data[0].texCoords, gl_TessCoord.xy);

  pos = attributes.position.xyz;
  texCoord = attributes.texCoord;

  gl_Position = projView * position;
}
