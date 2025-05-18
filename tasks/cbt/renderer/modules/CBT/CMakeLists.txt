
add_library(cbt_module CBTree.cpp)

target_include_directories(cbt_module PUBLIC ..)

target_include_directories(cbt_module INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(cbt_module INTERFACE shaders)

target_link_libraries(cbt_module PUBLIC etna render_utils gui)

target_add_shaders(cbt_module
    shaders/decoy.vert
    shaders/subdivision_split.tesc
    shaders/subdivision_merge.tesc
    shaders/process.tese
    shaders/terrain.frag
)