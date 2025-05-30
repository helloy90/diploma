
add_library(water_render_cbt_module WaterRenderModule.cpp)

target_include_directories(water_render_cbt_module PUBLIC ..)

target_include_directories(water_render_cbt_module INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(water_render_cbt_module INTERFACE shaders)

target_link_libraries(water_render_cbt_module PUBLIC etna render_utils gui cbt_module)

target_add_shaders(water_render_cbt_module
    shaders/decoy.vert
    shaders/subdivision_split.tesc
    shaders/subdivision_merge.tesc
    shaders/process.tese
    shaders/water.frag
)