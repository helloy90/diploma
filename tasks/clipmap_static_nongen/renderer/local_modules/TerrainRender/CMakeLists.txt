
add_library(terrain_render_nongen_module TerrainRenderModule.cpp)

target_include_directories(terrain_render_nongen_module PUBLIC ..)

target_include_directories(terrain_render_nongen_module INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(terrain_render_nongen_module INTERFACE shaders)

target_link_libraries(terrain_render_nongen_module PUBLIC etna render_utils gui tinygltf)

target_add_shaders(terrain_render_nongen_module
    shaders/clipmap.vert
    shaders/clipmap.frag
    shaders/clipmap_contrast.vert
    shaders/clipmap_contrast.frag
    shaders/culling.comp
)