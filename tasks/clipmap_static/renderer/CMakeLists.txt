
add_executable(project_renderer_static
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

target_link_libraries(project_renderer_static
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  lights_module terrain_generator_module terrain_render_module
)

target_add_shaders(project_renderer_static
  shaders/decoy.vert
  shaders/shading.frag
)
