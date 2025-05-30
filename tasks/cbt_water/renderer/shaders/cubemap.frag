#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform samplerCube cubemap;

layout(push_constant) uniform push_constant_t
{
  mat4 invProjViewMat3;
  uvec2 resolution;
};

void main()
{
  const vec4 screenSpacePosition = vec4(gl_FragCoord.xy / vec2(resolution) * 2.0 - 1.0, 1.0, 1.0);
  fragColor = texture(cubemap, normalize((invProjViewMat3 * screenSpacePosition).xyz));
}
