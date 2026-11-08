#include "solver_3d.h"

#include <engine/ecs/physics_3d.h>

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

void tangent_basis(glm::vec3 normal, glm::vec3& tangent, glm::vec3& bitangent) {
    const glm::vec3 reference = std::abs(normal.y) < 0.9f ? glm::vec3{0.f, 1.f, 0.f} : glm::vec3{1.f, 0.f, 0.f};
    tangent = glm::cross(normal, reference);
    float length = glm::length(tangent);
    if (length == 0.f) {
        tangent = glm::cross(normal, glm::vec3{0.f, 0.f, 1.f});
        length = glm::length(tangent);
    }
    if (length == 0.f) {
        tangent = glm::vec3{1.f, 0.f, 0.f};
        bitangent = glm::vec3{0.f, 0.f, 1.f};
        return;
    }
    tangent /= length;
    bitangent = glm::cross(normal, tangent);
}

[[nodiscard]] float inverse_along(const SolverBody3D& body, glm::vec3 lever) {
    return glm::dot(lever, body.inverse_inertia * lever);
}

[[nodiscard]] glm::vec3 velocity_at(const SolverBody3D& body, glm::vec3 lever) {
    return body.velocity + glm::cross(body.angular_velocity, lever);
}

void apply_impulse(SolverBody3D& body, glm::vec3 direction, float scalar, glm::vec3 lever) {
    if (!body.dynamic || body.inverse_mass <= 0.f || scalar == 0.f) {
        return;
    }
    body.velocity += direction * (scalar * body.inverse_mass);
    body.angular_velocity += body.inverse_inertia * glm::cross(lever, direction * scalar);
}

}

ContactShape3D contact_shape_3d(const SolverShape3D& shape, glm::vec3 center) {
    ContactShape3D out;
    out.center = center;
    out.kind = shape.kind;
    out.rotation = shape.rotation;
    out.half_extents = shape.half_extents;
    out.radius = shape.radius;
    out.height = shape.height;
    return out;
}

void solve_position_iteration_3d(std::span<SolverBody3D> bodies, std::span<const SolverShape3D> shapes,
                                 std::span<const SolverPair3D> pairs, std::span<ContactImpulse3D> impulses) {
    for (std::size_t pair_index = 0; pair_index < pairs.size(); ++pair_index) {
        const SolverPair3D& pair = pairs[pair_index];
        const SolverShape3D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape3D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        if (shape_a.body == shape_b.body) {
            continue;
        }
        SolverBody3D& body_a = bodies[static_cast<std::size_t>(shape_a.body)];
        SolverBody3D& body_b = bodies[static_cast<std::size_t>(shape_b.body)];
        const ContactManifold3D manifold = make_contact_3d(contact_shape_3d(shape_a, body_a.position),
                                                           contact_shape_3d(shape_b, body_b.position));
        if (manifold.count == 0) {
            continue;
        }

        const glm::vec3 normal = manifold.normal;
        ContactImpulse3D& accumulated = impulses[pair_index];
        accumulated.touched = true;
        accumulated.normal_axis = normal;

        const glm::vec3 origin_a = body_a.position;
        const glm::vec3 origin_b = body_b.position;
        const float depth = std::max(0.f, manifold.points[0].penetration - kContactSlop);
        const float inverse = body_a.inverse_mass + body_b.inverse_mass;
        if (inverse != 0.f && depth != 0.f) {
            const float correction = depth / inverse;
            if (body_a.dynamic && body_a.inverse_mass > 0.f) {
                body_a.position -= normal * (correction * body_a.inverse_mass);
            }
            if (body_b.dynamic && body_b.inverse_mass > 0.f) {
                body_b.position += normal * (correction * body_b.inverse_mass);
            }
        }
        if (inverse == 0.f) {
            continue;
        }

        glm::vec3 tangents[2];
        tangent_basis(normal, tangents[0], tangents[1]);
        const float friction = combined_friction(shape_a.friction, shape_b.friction);
        const float base_restitution = combined_restitution(shape_a.restitution, shape_b.restitution);
        const glm::vec3 point = manifold.points[0].point;
        const glm::vec3 lever_a = point - origin_a;
        const glm::vec3 lever_b = point - origin_b;
        const glm::vec3 relative = velocity_at(body_b, lever_b) - velocity_at(body_a, lever_a);
        const float normal_speed = glm::dot(relative, normal);
        const float restitution = normal_speed >= -kBounceThreshold ? 0.f : base_restitution;

        const glm::vec3 normal_lever_a = glm::cross(lever_a, normal);
        const glm::vec3 normal_lever_b = glm::cross(lever_b, normal);
        const float inverse_normal = body_a.inverse_mass + body_b.inverse_mass + inverse_along(body_a, normal_lever_a) +
                                     inverse_along(body_b, normal_lever_b);

        float normal_delta = 0.f;
        if (inverse_normal != 0.f && normal_speed < 0.f) {
            normal_delta = -(1.f + restitution) * normal_speed / inverse_normal;
        }
        if (accumulated.point.normal + normal_delta < 0.f) {
            normal_delta = -accumulated.point.normal;
        }
        accumulated.point.normal += normal_delta;

        float tangent_delta[2]{};
        glm::vec3 tangent_levers_a[2];
        glm::vec3 tangent_levers_b[2];
        const float tangent_limit = friction * accumulated.point.normal;
        for (int axis = 0; axis < 2; ++axis) {
            const glm::vec3 direction = tangents[axis];
            tangent_levers_a[axis] = glm::cross(lever_a, direction);
            tangent_levers_b[axis] = glm::cross(lever_b, direction);
            const float inverse_tangent = body_a.inverse_mass + body_b.inverse_mass +
                                          inverse_along(body_a, tangent_levers_a[axis]) +
                                          inverse_along(body_b, tangent_levers_b[axis]);
            const float tangent_speed = glm::dot(relative, direction);
            float candidate = accumulated.point.tangent[axis];
            if (inverse_tangent != 0.f) {
                candidate = accumulated.point.tangent[axis] + (-tangent_speed / inverse_tangent);
            }
            const float clamped = std::clamp(candidate, -tangent_limit, tangent_limit);
            tangent_delta[axis] = clamped - accumulated.point.tangent[axis];
            accumulated.point.tangent[axis] = clamped;
        }

        apply_impulse(body_a, normal, -normal_delta, lever_a);
        apply_impulse(body_b, normal, normal_delta, lever_b);
        for (int axis = 0; axis < 2; ++axis) {
            apply_impulse(body_a, tangents[axis], -tangent_delta[axis], lever_a);
            apply_impulse(body_b, tangents[axis], tangent_delta[axis], lever_b);
        }
    }
}

}
