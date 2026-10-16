#pragma once

// docs/tech/modules/ECS.md (Camera)

#include <glm/vec3.hpp>

namespace engine {

// A half-line in world space. `direction` is unit length when it comes from `screen_to_ray`.
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

}
