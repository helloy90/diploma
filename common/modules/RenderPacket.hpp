#pragma once

#include <glm/glm.hpp>

struct RenderPacket {
    glm::mat4x4 projView;
    glm::mat4x4 view;
    glm::mat4x4 proj;
    glm::vec3 cameraWorldPosition;
    float time;
};