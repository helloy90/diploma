#version 460
#extension GL_ARB_separate_shader_objects : enable

void main() {
    // vec2 pos = gl_VertexIndex == 0 ? vec2(-1024, -1024) : (gl_VertexIndex == 1 ? vec2(-1024, 1024) : (gl_VertexIndex == 2 ? vec2(1024, -1024) : vec2(1024, 1024))); // assume quad
    // gl_Position = vec4(pos, 0, 1);
}