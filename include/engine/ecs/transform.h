#pragma once

// docs/tech/modules/ECS.md

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace engine {

// Local transform. Under a `Parent` it is relative to the parent's space (docs/tech/modules/ECS.md, Hierarchy).
struct Transform {
    glm::vec3 position{0};
    glm::vec3 rotation{0};
    glm::vec3 scale{1};
};

// Translate * RotateX * RotateY * RotateZ * Scale of the local transform.
[[nodiscard]] glm::mat4 model_matrix(const Transform& transform);

}
