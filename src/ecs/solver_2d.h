#pragma once

// Private. One positional iteration over XY bodies. No World and no events. Not installed.

#include "contact_2d.h"

#include <span>

namespace engine {

struct SolverBody2D {
    glm::vec2 position{0.f};
    glm::vec2 velocity{0.f};  // world XY; static is 0; kinematic is already converted
    float inverse_mass{0.f};
    bool dynamic{false};  // only dynamics receive position and velocity writes
};

// One collider attached to a body. Two shapes may share a body index.
struct SolverShape2D {
    int body{0};
    ContactShapeKind kind{ContactShapeKind::Aabb};
    glm::vec2 half_extents{0.f};
    float radius{0.f};
};

// shape_a / shape_b are indices into the shape span. The caller orders them so entity A < entity B.
struct SolverPair2D {
    int shape_a{0};
    int shape_b{0};
};

// Recomputes make_contact_2d from the current body positions and writes corrections back into `bodies`.
void solve_position_iteration_2d(std::span<SolverBody2D> bodies, std::span<const SolverShape2D> shapes,
                                 std::span<const SolverPair2D> pairs);

}
