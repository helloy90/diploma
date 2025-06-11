
add_executable(project_renderer_water
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

add_subdirectory(local_modules)

target_link_libraries(project_renderer_water
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  lights_module water_generator_module water_render_module
)

target_add_shaders(project_renderer_water
  shaders/cubemap.vert
  shaders/cubemap.frag
)
