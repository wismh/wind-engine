#pragma once

// Private. One positional iteration over 3D bodies. No World and no events. Not installed.

#include "contact_3d.h"

#include <glm/mat3x3.hpp>

#include <span>

namespace engine {

struct SolverBody3D {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};          // world; static is 0; kinematic is already converted
    glm::vec3 angular_velocity{0.f};  // world axes
    float inverse_mass{0.f};
    glm::mat3 inverse_inertia{0.f};  // zero for kinematic, static, and a degenerate dynamic shape
    bool dynamic{false};
};

// One collider on a body. `rotation` is the world quaternion, held fixed for the iteration.
struct SolverShape3D {
    int body{0};
    ContactShapeKind3D kind{ContactShapeKind3D::Sphere};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 half_extents{0.f};
    float radius{0.f};
    float height{0.f};
    float friction{0.f};
    float restitution{0.f};
};

struct SolverPair3D {
    int shape_a{0};
    int shape_b{0};
};

struct ContactPointImpulse3D {
    float normal{0.f};
    float tangent[2]{};
};

struct ContactImpulse3D {
    ContactPointImpulse3D point{};
    glm::vec3 normal_axis{0.f};
    bool touched{false};
};

[[nodiscard]] ContactShape3D contact_shape_3d(const SolverShape3D& shape, glm::vec3 center);

// Recomputes the contact from the current body positions. Positional correction uses the one point
// and the mass sum. Velocity uses that same point, with two tangent axes.
void solve_position_iteration_3d(std::span<SolverBody3D> bodies, std::span<const SolverShape3D> shapes,
                                 std::span<const SolverPair3D> pairs, std::span<ContactImpulse3D> impulses);

}
