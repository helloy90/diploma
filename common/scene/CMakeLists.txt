
add_library(scene TerrainManager.cpp)

target_include_directories(scene PUBLIC ..)

target_link_libraries(scene PUBLIC glm::glm etna)
