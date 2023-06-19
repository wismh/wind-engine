#pragma once

#include <glm/vec3.hpp>

namespace engine {

struct Transform {
    glm::vec3 position{0};
    glm::vec3 rotation{0};
    glm::vec3 scale{1};
};

}
