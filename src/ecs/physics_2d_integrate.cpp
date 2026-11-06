#include "physics_2d_internal.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/transform.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <vector>

namespace engine {
namespace {

struct DynamicTarget {
    ecs::Entity entity{};
    int depth{0};
    glm::vec3 position{0.f};
};

// Walks Parent. Roots are 0. A hand-made cycle stops at kMaxHierarchyDepth.
[[nodiscard]] int hierarchy_depth(const ecs::World& world, ecs::Entity entity) {
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

// Root-to-entity quaternion product. Scale is not part of this chain.
[[nodiscard]] glm::quat world_rotation_quat(const ecs::World& world, ecs::Entity entity) {
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

// A zero turn is skipped so an identity quaternion stays identity.
void apply_world_yaw(ecs::World& world, ecs::Entity entity, float dtheta) {
    if (dtheta == 0.f) {
        return;
    }
    Transform* transform = world.try_get<Transform>(entity);
    if (transform == nullptr) {
        return;
    }
    const Parent* parent = world.try_get<Parent>(entity);
    if (parent == nullptr || !world.valid(parent->entity)) {
        transform->rotation = rotation_z(dtheta) * transform->rotation;
        return;
    }
    const glm::quat parent_world = world_rotation_quat(world, parent->entity);
    const glm::quat world_rotation = parent_world * transform->rotation;
    const glm::quat turned = rotation_z(dtheta) * world_rotation;
    transform->rotation = glm::normalize(glm::inverse(parent_world) * turned);
}

}

void integrate_kinematic_2d(ecs::World& world, float dt) {
    world.view<Transform, RigidBody2D>().each(
            [&world, dt](ecs::Entity entity, Transform& transform, const RigidBody2D& body) {
        // A 3D body is integrated by run_physics_3d. Do not move it here as well.
        if (world.try_get<RigidBody3D>(entity) != nullptr) {
            return;
        }
        if (body.type != BodyType2D::Kinematic) {
            return;
        }
        transform.position += body.velocity * dt;
    });
}

void integrate_dynamic_2d(ecs::World& world, float dt) {
    const glm::vec3 gravity = world.ctx<Physics2DConfig>().gravity;
    std::vector<ecs::Entity> dynamics;
    world.view<Transform, RigidBody2D>().each([&](ecs::Entity entity, const Transform&, const RigidBody2D& body) {
        if (world.try_get<RigidBody3D>(entity) != nullptr) {
            return;
        }
        if (body.type == BodyType2D::Dynamic && !body.sleeping) {
            dynamics.push_back(entity);
        }
    });

    // Snapshot every target before any write. A dynamic parent must not drag a dynamic child: each target is an
    // absolute world position, and parents are written first so set_world_position sees the new parent chain.
    std::vector<DynamicTarget> targets;
    targets.reserve(dynamics.size());
    for (ecs::Entity entity : dynamics) {
        RigidBody2D& body = world.get<RigidBody2D>(entity);
        body.velocity += gravity * body.gravity_scale * dt;
        const glm::vec3 position = ecs::world_position(world, entity) + body.velocity * dt;
        targets.push_back(DynamicTarget{entity, hierarchy_depth(world, entity), position});
    }
    std::stable_sort(targets.begin(), targets.end(), [](const DynamicTarget& a, const DynamicTarget& b) {
        return a.depth < b.depth;
    });
    for (const DynamicTarget& target : targets) {
        ecs::set_world_position(world, target.entity, target.position);
    }
    // Yaw after the linear write. The turn touches rotation only, so the position just stored stays put.
    // Parents first: a child's world yaw is measured against the parent's updated quaternion.
    for (const DynamicTarget& target : targets) {
        const RigidBody2D& body = world.get<RigidBody2D>(target.entity);
        apply_world_yaw(world, target.entity, body.angular_velocity * dt);
    }
}

}
