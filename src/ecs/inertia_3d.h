#pragma once

// Private. Local inertia about the body origin, then the world-space inverse tensor. Not installed.

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace engine {

// Diagonal in the shape's local axes. Zero when mass is not positive or the shape is degenerate.
// Box uses the full size: m/12 * (y^2+z^2, z^2+x^2, x^2+y^2).
[[nodiscard]] glm::vec3 inertia_3d_box(float mass, glm::vec3 size);

// Solid sphere: 0.4 * mass * radius^2 on every axis.
[[nodiscard]] glm::vec3 inertia_3d_sphere(float mass, float radius);

// Uniform-density cylinder of length `height` plus two hemispheres (one sphere of radius `radius`).
// `height == 0` matches inertia_3d_sphere. A non-positive radius or a negative height is zero.
[[nodiscard]] glm::vec3 inertia_3d_capsule(float mass, float radius, float height);

// R * diag(1/Ii) * R^T. A non-positive Ii contributes 0 on that local axis (locked).
[[nodiscard]] glm::mat3 inverse_inertia_world(glm::quat rotation, glm::vec3 inertia);

}
