
add_executable(project_renderer_static_nongen
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

add_subdirectory(local_modules)

target_link_libraries(project_renderer_static_nongen
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  lights_module terrain_generator_module terrain_render_nongen_module
)

target_add_shaders(project_renderer_static_nongen
  shaders/decoy.vert
  shaders/shading.frag
)
