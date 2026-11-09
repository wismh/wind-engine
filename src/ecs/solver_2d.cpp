#include "solver_2d.h"

#include <engine/ecs/physics_2d.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

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

[[nodiscard]] float lever(glm::vec2 r, glm::vec2 direction) {
    return r.x * direction.y - r.y * direction.x;
}

[[nodiscard]] glm::vec2 velocity_at(const SolverBody2D& body, glm::vec2 r) {
    return body.velocity + body.angular_velocity * glm::vec2{-r.y, r.x};
}

void apply_impulse(SolverBody2D& body, glm::vec2 direction, float scalar, float lever_arm) {
    if (!body.dynamic || body.inverse_mass <= 0.f || scalar == 0.f) {
        return;
    }
    body.velocity += direction * (scalar * body.inverse_mass);
    body.angular_velocity += lever_arm * scalar * body.inverse_inertia;
}

}

ContactShape2D contact_shape_2d(const SolverShape2D& shape, glm::vec2 center) {
    ContactShape2D out;
    out.center = center;
    out.kind = shape.kind;
    out.half_extents = shape.half_extents;
    out.radius = shape.radius;
    out.angle = shape.angle;
    out.vertex_count = shape.vertex_count;
    out.sdf = shape.sdf;
    out.sdf_chunk = shape.sdf_chunk;
    if (shape.kind != ContactShapeKind::Polygon || shape.vertex_count <= 0) {
        return out;
    }
    const float c = std::cos(shape.angle);
    const float s = std::sin(shape.angle);
    const int count = std::min(shape.vertex_count, kContactPolygonCapacity);
    out.vertex_count = count;
    for (int i = 0; i < count; ++i) {
        const glm::vec2 local = shape.vertices[i];
        out.vertices[i] = center + glm::vec2{c * local.x - s * local.y, s * local.x + c * local.y};
    }
    return out;
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
                make_contact_2d(contact_shape_2d(shape_a, body_a.position), contact_shape_2d(shape_b, body_b.position));
        if (manifold.count == 0) {
            continue;
        }

        const glm::vec2 normal = manifold.normal;
        ContactImpulse2D& accumulated = impulses[pair_index];
        accumulated.touched = true;
        accumulated.normal_axis = normal;

        // Lever arms use the pose that produced the manifold, before the positional push.
        const glm::vec2 origin_a = body_a.position;
        const glm::vec2 origin_b = body_b.position;
        const float depth = std::max(0.f, manifold.points[0].penetration - kContactSlop);
        const float inverse = body_a.inverse_mass + body_b.inverse_mass;
        if (inverse > 0.f && depth > 0.f && std::isfinite(depth)) {
            glm::vec2 pos_dir = normal;
            if (std::abs(pos_dir.x) < 0.2f) {
                pos_dir = glm::vec2{0.f, std::copysign(1.f, pos_dir.y)};
            } else if (std::abs(pos_dir.y) < 0.2f) {
                pos_dir = glm::vec2{std::copysign(1.f, pos_dir.x), 0.f};
            }
            const float correction = depth / inverse;
            if (body_a.dynamic && body_a.inverse_mass > 0.f) {
                body_a.position -= pos_dir * (correction * body_a.inverse_mass);
            }
            if (body_b.dynamic && body_b.inverse_mass > 0.f) {
                body_b.position += pos_dir * (correction * body_b.inverse_mass);
            }
        }
        if (inverse <= 0.f) {
            continue;
        }

        const glm::vec2 tangent{-normal.y, normal.x};
        const float friction = combined_friction(shape_a.friction, shape_b.friction);
        const float base_restitution = combined_restitution(shape_a.restitution, shape_b.restitution);
        const int point_count = std::min(manifold.count, kContact2DPointCapacity);
        for (int point_index = 0; point_index < point_count; ++point_index) {
            const glm::vec2 point = manifold.points[point_index].point;
            const glm::vec2 r_a = point - origin_a;
            const glm::vec2 r_b = point - origin_b;
            const float rn_a = lever(r_a, normal);
            const float rn_b = lever(r_b, normal);
            const float rt_a = lever(r_a, tangent);
            const float rt_b = lever(r_b, tangent);
            const float inv_n = body_a.inverse_mass + body_b.inverse_mass + rn_a * rn_a * body_a.inverse_inertia +
                                rn_b * rn_b * body_b.inverse_inertia;
            const float inv_t = body_a.inverse_mass + body_b.inverse_mass + rt_a * rt_a * body_a.inverse_inertia +
                                rt_b * rt_b * body_b.inverse_inertia;
            if (inv_n <= 0.f && inv_t <= 0.f) {
                continue;
            }

            const glm::vec2 relative = velocity_at(body_b, r_b) - velocity_at(body_a, r_a);
            const float normal_speed = glm::dot(relative, normal);
            const float tangent_speed = glm::dot(relative, tangent);
            float restitution = normal_speed >= -kBounceThreshold ? 0.f : base_restitution;

            ContactPointImpulse2D& point_impulse = accumulated.points[point_index];
            float normal_delta = 0.f;
            if (inv_n > 0.f && (normal_speed < 0.f || base_restitution == 0.f)) {
                normal_delta = -(1.f + restitution) * normal_speed / inv_n;
            }
            if (point_impulse.normal + normal_delta < 0.f) {
                normal_delta = -point_impulse.normal;
            }
            point_impulse.normal += normal_delta;

            const float tangent_limit = friction * point_impulse.normal;
            float tangent_candidate = point_impulse.tangent;
            if (inv_t > 0.f) {
                tangent_candidate = point_impulse.tangent + (-tangent_speed / inv_t);
            }
            const float clamped_tangent = std::clamp(tangent_candidate, -tangent_limit, tangent_limit);
            const float tangent_delta = clamped_tangent - point_impulse.tangent;
            point_impulse.tangent = clamped_tangent;

            // Equal weight: each point uses the full inverse mass. A is pushed by -j, B by +j.
            // ω_a -= rn * j * inv_I and ω_b += rn * j * inv_I, with rt in place of rn for the tangent.
            apply_impulse(body_a, normal, -normal_delta, rn_a);
            apply_impulse(body_a, tangent, -tangent_delta, rt_a);
            apply_impulse(body_b, normal, normal_delta, rn_b);
            apply_impulse(body_b, tangent, tangent_delta, rt_b);
        }
    }
}

}
