#include <engine/render/aabb.h>

#include <glm/common.hpp>

namespace engine::render {

Aabb transform_aabb(const Aabb& box, const glm::mat4& transform) {
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const glm::vec3 half = (box.max - box.min) * 0.5f;
    const glm::vec3 moved = glm::vec3(transform * glm::vec4(center, 1.0f));
    glm::vec3 extent{0.0f};
    for (int column = 0; column < 3; ++column) {
        extent += glm::abs(glm::vec3(transform[column])) * half[column];
    }
    return Aabb{.min = moved - extent, .max = moved + extent};
}

}
