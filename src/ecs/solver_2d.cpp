#include "solver_2d.h"

#include <engine/ecs/physics_2d.h>

#include <glm/geometric.hpp>

#include <algorithm>

namespace engine {
namespace {

ContactShape2D contact_shape(const SolverShape2D& shape, glm::vec2 center) {
    ContactShape2D out;
    out.center = center;
    out.kind = shape.kind;
    out.half_extents = shape.half_extents;
    out.radius = shape.radius;
    return out;
}

}

void solve_position_iteration_2d(std::span<SolverBody2D> bodies, std::span<const SolverShape2D> shapes,
                                 std::span<const SolverPair2D> pairs) {
    for (const SolverPair2D& pair : pairs) {
        const SolverShape2D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape2D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        if (shape_a.body == shape_b.body) {
            continue;
        }
        SolverBody2D& body_a = bodies[static_cast<std::size_t>(shape_a.body)];
        SolverBody2D& body_b = bodies[static_cast<std::size_t>(shape_b.body)];
        const ContactManifold2D manifold =
                make_contact_2d(contact_shape(shape_a, body_a.position), contact_shape(shape_b, body_b.position));
        if (manifold.count == 0) {
            continue;
        }

        const glm::vec2 normal = manifold.normal;
        const float depth = std::max(0.f, manifold.points[0].penetration - kContactSlop2D);
        const float inverse = body_a.inverse_mass + body_b.inverse_mass;
        if (inverse != 0.f && depth != 0.f) {
            const float correction = depth / inverse;
            if (body_a.dynamic) {
                body_a.position -= normal * (correction * body_a.inverse_mass);
            }
            if (body_b.dynamic) {
                body_b.position += normal * (correction * body_b.inverse_mass);
            }
        }
        if (inverse == 0.f) {
            continue;
        }
        const float relative = glm::dot(body_b.velocity - body_a.velocity, normal);
        if (relative < 0.f) {
            const float impulse = -relative / inverse;
            if (body_a.dynamic) {
                body_a.velocity -= normal * (impulse * body_a.inverse_mass);
            }
            if (body_b.dynamic) {
                body_b.velocity += normal * (impulse * body_b.inverse_mass);
            }
        }
    }
}

}
