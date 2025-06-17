
add_executable(project_renderer_cbt_nongen
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

add_subdirectory(local_modules)

target_link_libraries(project_renderer_cbt_nongen
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  lights_module terrain_render_cbt_nongen_module 
)

target_add_shaders(project_renderer_cbt_nongen
  shaders/decoy.vert
  shaders/shading.frag
  shaders/shading_contrast.frag
)
