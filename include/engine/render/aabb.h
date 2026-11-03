#pragma once

// docs/tech/modules/Render.md (Frustum culling)

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace engine::render {

// An axis-aligned box, `min <= max` on every axis.
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    bool operator==(const Aabb&) const = default;
};

// The smallest axis-aligned box around `box` after the affine `transform` (a model or world matrix): the center goes
// through the matrix, the half extents through its absolute 3x3. A rotated box grows; a negative (flip) or zero scale
// still gives `min <= max`.
[[nodiscard]] Aabb transform_aabb(const Aabb& box, const glm::mat4& transform);

}
