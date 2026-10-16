#pragma once

// docs/tech/modules/ECS.md

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace engine {

// Local transform. Under a `Parent` it is relative to the parent's space (docs/tech/modules/ECS.md, Hierarchy).
// `rotation` is a unit quaternion; identity by default. A 2D game turns about Z with `rotation_z`.
struct Transform {
    glm::vec3 position{0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1};
};

// Translate * mat4_cast(rotation) * Scale of the local transform.
[[nodiscard]] glm::mat4 model_matrix(const Transform& transform);

// A turn of `radians` about +Z (counter-clockwise in a 2D view looking down -Z).
[[nodiscard]] glm::quat rotation_z(float radians);

// Rx(x) * Ry(y) * Rz(z) from Euler radians: the rotation applied Z first, then Y, then X.
[[nodiscard]] glm::quat euler_xyz(glm::vec3 radians);

// The 2D angle of `rotation` about +Z in radians, (-pi, pi]: the direction of the rotated +X axis in the XY plane.
// Exact inverse of `rotation_z`. For a 3D rotation it is the heading of the rotated +X axis; 0 when that axis is +-Z.
[[nodiscard]] float angle_z(const glm::quat& rotation);

}
