
add_library(terrain_render_module TerrainRenderModule.cpp)

target_include_directories(terrain_render_module PUBLIC ..)

target_include_directories(terrain_render_module INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(terrain_render_module INTERFACE shaders)

target_link_libraries(terrain_render_module PUBLIC etna render_utils gui cbt_module)


target_add_shaders(terrain_render_module
    shaders/decoy.vert
    shaders/subdivision_split.tesc
    shaders/subdivision_merge.tesc
    shaders/process.tese
    shaders/terrain.frag
)