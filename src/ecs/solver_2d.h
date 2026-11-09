#pragma once

// Private. One positional iteration over XY bodies. No World and no events. Not installed.

#include "contact_2d.h"

#include <span>

namespace engine {

struct SolverBody2D {
    glm::vec2 position{0.f};
    glm::vec2 velocity{0.f};  // world XY; static is 0; kinematic is already converted
    float angular_velocity{0.f};
    float inverse_mass{0.f};
    float inverse_inertia{0.f};  // 0 for kinematic, static, and a degenerate dynamic shape
    bool dynamic{false};         // only dynamics receive position, velocity, and omega writes
};

// One collider attached to a body. Two shapes may share a body index.
// Polygon vertices are local, about the body origin. `angle` is the world Z yaw, held fixed
// for the iteration (yaw is integrated before the solver). friction and restitution are raw.
struct SolverShape2D {
    int body{0};
    ContactShapeKind kind{ContactShapeKind::Aabb};
    glm::vec2 half_extents{0.f};
    float radius{0.f};
    float angle{0.f};
    glm::vec2 vertices[kContactPolygonCapacity]{};
    int vertex_count{0};
    float friction{0.f};
    float restitution{0.f};
    // Sdf only: the field and the chunk this shape is (ContactShape2D).
    const SdfField2D* sdf{nullptr};
    glm::ivec2 sdf_chunk{0};
};

// shape_a / shape_b are indices into the shape span. The caller orders them so entity A < entity B.
struct SolverPair2D {
    int shape_a{0};
    int shape_b{0};
};

// One impulse pair per contact point. The caller seeds both. This iteration adds deltas and clamps.
// touched and normal_axis record the last manifold that overlapped during the iteration.
struct ContactPointImpulse2D {
    float normal{0.f};
    float tangent{0.f};
};

struct ContactImpulse2D {
    ContactPointImpulse2D points[kContact2DPointCapacity]{};
    glm::vec2 normal_axis{0.f};
    bool touched{false};
};

// World-space shape at `center`. Polygon vertices are rotated by the stored world yaw.
[[nodiscard]] ContactShape2D contact_shape_2d(const SolverShape2D& shape, glm::vec2 center);

// Recomputes the contact from the current body positions and writes corrections back into `bodies`.
// Positional correction uses points[0] and the mass sum. Velocity uses both points.
// `impulses` is one entry per pair. A pair with no manifold leaves its accumulator unchanged.
void solve_position_iteration_2d(std::span<SolverBody2D> bodies, std::span<const SolverShape2D> shapes,
                                 std::span<const SolverPair2D> pairs, std::span<ContactImpulse2D> impulses);

}
