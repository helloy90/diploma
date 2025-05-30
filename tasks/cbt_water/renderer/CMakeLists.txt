
add_executable(project_renderer_water_cbt
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

target_link_libraries(project_renderer_water_cbt
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  lights_module water_generator_module water_render_cbt_module 
)

target_add_shaders(project_renderer_water_cbt
  shaders/cubemap.vert
  shaders/cubemap.frag
)
