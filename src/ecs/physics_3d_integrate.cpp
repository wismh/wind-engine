#include "physics_3d_internal.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {
namespace {

struct DynamicTarget {
    ecs::Entity entity{};
    int depth{0};
    glm::vec3 position{0.f};
};

// A zero spin is skipped so an identity quaternion stays identity.
void apply_world_spin(ecs::World& world, ecs::Entity entity, glm::vec3 omega_dt) {
    if (omega_dt == glm::vec3{0.f}) {
        return;
    }
    if (!std::isfinite(omega_dt.x) || !std::isfinite(omega_dt.y) || !std::isfinite(omega_dt.z)) {
        return;
    }
    const float angle = glm::length(omega_dt);
    if (angle == 0.f) {
        return;
    }
    Transform* transform = world.try_get<Transform>(entity);
    if (transform == nullptr) {
        return;
    }
    const glm::quat delta = glm::angleAxis(angle, omega_dt / angle);
    const Parent* parent = world.try_get<Parent>(entity);
    if (parent == nullptr || !world.valid(parent->entity)) {
        transform->rotation = delta * transform->rotation;
        return;
    }
    const glm::quat parent_world = world_rotation_quat_3d(world, parent->entity);
    const glm::quat world_rotation = parent_world * transform->rotation;
    const glm::quat turned = delta * world_rotation;
    transform->rotation = glm::normalize(glm::inverse(parent_world) * turned);
}

}

int hierarchy_depth_3d(const ecs::World& world, ecs::Entity entity) {
    int depth = 0;
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth; ++step) {
        const Parent* parent = world.try_get<Parent>(current);
        if (parent == nullptr || !world.valid(parent->entity)) {
            break;
        }
        current = parent->entity;
        ++depth;
    }
    return depth;
}

glm::quat world_rotation_quat_3d(const ecs::World& world, ecs::Entity entity) {
    glm::quat chain[ecs::kMaxHierarchyDepth];
    int count = 0;
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
        const Transform* transform = world.try_get<Transform>(current);
        chain[count++] = transform != nullptr ? transform->rotation : glm::quat{1.f, 0.f, 0.f, 0.f};
        const Parent* parent = world.try_get<Parent>(current);
        if (parent == nullptr || !world.valid(parent->entity)) {
            break;
        }
        current = parent->entity;
    }
    glm::quat world_rotation{1.f, 0.f, 0.f, 0.f};
    for (int i = count - 1; i >= 0; --i) {
        world_rotation = world_rotation * chain[i];
    }
    return world_rotation;
}

void integrate_kinematic_3d(ecs::World& world, float dt) {
    world.view<Transform, RigidBody3D>().each([dt](Transform& transform, const RigidBody3D& body) {
        if (body.type != BodyType3D::Kinematic) {
            return;
        }
        transform.position += body.velocity * dt;
    });
}

void integrate_dynamic_3d(ecs::World& world, float dt) {
    const glm::vec3 gravity = world.ctx<Physics3DConfig>().gravity;
    std::vector<ecs::Entity> dynamics;
    world.view<Transform, RigidBody3D>().each([&](ecs::Entity entity, const Transform&, const RigidBody3D& body) {
        if (body.type == BodyType3D::Dynamic && !body.sleeping) {
            dynamics.push_back(entity);
        }
    });

    std::vector<DynamicTarget> targets;
    targets.reserve(dynamics.size());
    for (ecs::Entity entity : dynamics) {
        RigidBody3D& body = world.get<RigidBody3D>(entity);
        body.velocity += gravity * body.gravity_scale * dt;
        const glm::vec3 position = ecs::world_position(world, entity) + body.velocity * dt;
        targets.push_back(DynamicTarget{entity, hierarchy_depth_3d(world, entity), position});
    }
    std::stable_sort(targets.begin(), targets.end(), [](const DynamicTarget& a, const DynamicTarget& b) {
        return a.depth < b.depth;
    });
    for (const DynamicTarget& target : targets) {
        ecs::set_world_position(world, target.entity, target.position);
    }
    for (const DynamicTarget& target : targets) {
        const RigidBody3D& body = world.get<RigidBody3D>(target.entity);
        apply_world_spin(world, target.entity, body.angular_velocity * dt);
    }
}

}
