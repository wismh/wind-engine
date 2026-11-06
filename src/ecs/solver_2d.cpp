#include "solver_2d.h"

#include <engine/ecs/physics_2d.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

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

float clamped_friction(float value) {
    if (!std::isfinite(value) || value < 0.f) {
        return 0.f;
    }
    return value;
}

float clamped_restitution(float value) {
    if (!std::isfinite(value)) {
        return 0.f;
    }
    return std::clamp(value, 0.f, 1.f);
}

float combined_friction(float a, float b) {
    return std::sqrt(clamped_friction(a) * clamped_friction(b));
}

float combined_restitution(float a, float b) {
    return std::max(clamped_restitution(a), clamped_restitution(b));
}

}

void solve_position_iteration_2d(std::span<SolverBody2D> bodies, std::span<const SolverShape2D> shapes,
                                 std::span<const SolverPair2D> pairs, std::span<ContactImpulse2D> impulses) {
    for (std::size_t pair_index = 0; pair_index < pairs.size(); ++pair_index) {
        const SolverPair2D& pair = pairs[pair_index];
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
        ContactImpulse2D& accumulated = impulses[pair_index];
        accumulated.touched = true;
        accumulated.normal_axis = normal;

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

        const glm::vec2 tangent{-normal.y, normal.x};
        const glm::vec2 relative_velocity = body_b.velocity - body_a.velocity;
        const float normal_speed = glm::dot(relative_velocity, normal);
        const float tangent_speed = glm::dot(relative_velocity, tangent);
        float restitution = combined_restitution(shape_a.restitution, shape_b.restitution);
        if (normal_speed >= -kBounceThreshold2D) {
            restitution = 0.f;
        }
        float normal_delta = 0.f;
        if (normal_speed < 0.f) {
            normal_delta = -(1.f + restitution) * normal_speed / inverse;
        }
        if (accumulated.normal + normal_delta < 0.f) {
            normal_delta = -accumulated.normal;
        }
        accumulated.normal += normal_delta;

        const float friction = combined_friction(shape_a.friction, shape_b.friction);
        const float tangent_limit = friction * accumulated.normal;
        const float clamped_tangent =
                std::clamp(accumulated.tangent + (-tangent_speed / inverse), -tangent_limit, tangent_limit);
        const float tangent_delta = clamped_tangent - accumulated.tangent;
        accumulated.tangent = clamped_tangent;

        const glm::vec2 impulse = normal * normal_delta + tangent * tangent_delta;
        if (body_a.dynamic) {
            body_a.velocity -= impulse * body_a.inverse_mass;
        }
        if (body_b.dynamic) {
            body_b.velocity += impulse * body_b.inverse_mass;
        }
    }
}

}
