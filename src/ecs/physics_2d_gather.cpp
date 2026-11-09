#include "physics_2d_internal.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine {
namespace {

// Heading of the matrix X axis in XY. Same epsilon as angle_z. Scale changes the length, not the heading.
[[nodiscard]] float yaw_of(const glm::mat4& matrix) {
    const float x = matrix[0][0];
    const float y = matrix[0][1];
    if (std::abs(x) < 1.0e-6f && std::abs(y) < 1.0e-6f) {
        return 0.f;
    }
    return std::atan2(y, x);
}

void warn_dynamic_terrain(ecs::World& world, ecs::Entity entity) {
    const bool compound = world.try_get<CompoundCollider2D>(entity) != nullptr;
    const bool sdf = world.try_get<SdfCollider2D>(entity) != nullptr;
    if (!compound && !sdf) {
        return;
    }
    std::vector<ecs::Entity>& warned = world.ctx<PhysicsBroadphase2D>().dynamic_terrain_warned;
    const auto it = std::lower_bound(warned.begin(), warned.end(), entity);
    if (it != warned.end() && *it == entity) {
        return;
    }
    warned.insert(it, entity);
    const std::string what = compound && sdf ? "CompoundCollider2D and SdfCollider2D"
                             : compound      ? "CompoundCollider2D"
                                             : "SdfCollider2D";
    log::warn("physics 2D: the " + what + " on entity " + std::to_string(entity.index) +
              " is under a Dynamic RigidBody2D and is ignored. Compounds and SDF colliders are static or kinematic "
              "only.");
}

}

glm::mat4 collider_world_matrix_2d(const ecs::World& world, ecs::Entity entity, const Transform& transform) {
    if (const WorldTransform* cached = world.try_get<WorldTransform>(entity)) {
        return cached->matrix;
    }
    return model_matrix(transform);
}

ColliderItem2D make_box_item_2d(ecs::Entity entity, const glm::mat4& matrix, const BoxCollider2D& box, bool movable) {
    return ColliderItem2D{
            .entity = entity,
            .position = glm::vec3(matrix[3]),
            .angle = yaw_of(matrix),
            .shape = ColliderShape2D::Box,
            .box_size = box.size,
            .layer = box.layer,
            .mask = box.mask,
            .is_trigger = box.is_trigger,
            .friction = box.friction,
            .restitution = box.restitution,
            .movable = movable,
    };
}

ColliderItem2D make_circle_item_2d(ecs::Entity entity, const glm::mat4& matrix, const CircleCollider2D& circle,
                                   bool movable) {
    return ColliderItem2D{
            .entity = entity,
            .position = glm::vec3(matrix[3]),
            .angle = yaw_of(matrix),
            .shape = ColliderShape2D::Circle,
            .radius = circle.radius,
            .layer = circle.layer,
            .mask = circle.mask,
            .is_trigger = circle.is_trigger,
            .friction = circle.friction,
            .restitution = circle.restitution,
            .movable = movable,
    };
}

ColliderItem2D make_polygon_item_2d(ecs::Entity entity, const glm::mat4& matrix, const PolygonCollider2D& polygon,
                                    bool movable) {
    ColliderItem2D item{
            .entity = entity,
            .position = glm::vec3(matrix[3]),
            .angle = yaw_of(matrix),
            .shape = ColliderShape2D::Polygon,
            .layer = polygon.layer,
            .mask = polygon.mask,
            .is_trigger = polygon.is_trigger,
            .friction = polygon.friction,
            .restitution = polygon.restitution,
            .movable = movable,
    };
    item.polygon_count = std::min(static_cast<int>(polygon.count), kPolygonCollider2DCapacity);
    for (int i = 0; i < item.polygon_count; ++i) {
        item.polygon[i] = polygon.vertices[i];
    }
    return item;
}

ColliderItem2D make_compound_item_2d(ecs::Entity entity, const glm::mat4& matrix, const CompoundCollider2D& compound,
                                     std::uint32_t part, bool movable) {
    const CompoundShape2D& shape = compound.shapes[part];
    ColliderItem2D item{
            .entity = entity,
            .position = glm::vec3(matrix[3]),
            .angle = yaw_of(matrix),
            .shape = ColliderShape2D::Compound,
            .layer = compound.layer,
            .mask = compound.mask,
            .friction = compound.friction,
            .restitution = compound.restitution,
            .movable = movable,
            .part = part,
    };
    item.polygon_count = std::min(static_cast<int>(shape.count), kPolygonCollider2DCapacity);
    for (int i = 0; i < item.polygon_count; ++i) {
        item.polygon[i] = shape.vertices[i];
    }
    return item;
}

ColliderItem2D make_sdf_item_2d(ecs::Entity entity, const glm::mat4& matrix, const SdfCollider2D& sdf,
                                glm::ivec2 chunk, bool movable) {
    ColliderItem2D item{
            .entity = entity,
            .position = glm::vec3(matrix[3]),
            .angle = yaw_of(matrix),
            .shape = ColliderShape2D::Sdf,
            .layer = sdf.layer,
            .mask = sdf.mask,
            .friction = sdf.friction,
            .restitution = sdf.restitution,
            .movable = movable,
            .part = sdf_part_of_chunk_2d(chunk),
            .sdf = &sdf.field,
            .sdf_revision = sdf.field.revision(chunk),
    };
    // The chunk's cells in the field's frame, grown so a sample on the rectangle's edge is inside the AABB.
    const float cell = sdf.field.cell_size();
    const float grow = 0.01f * cell;
    const glm::vec2 lo = sdf.field.origin() + glm::vec2(chunk * kSdfChunkCells) * cell - grow;
    const glm::vec2 hi = lo + glm::vec2(static_cast<float>(kSdfChunkCells) * cell + 2.f * grow);
    item.polygon_count = 4;
    item.polygon[0] = lo;
    item.polygon[1] = {hi.x, lo.y};
    item.polygon[2] = hi;
    item.polygon[3] = {lo.x, hi.y};
    return item;
}

void append_sdf_items_2d(ecs::Entity entity, const glm::mat4& matrix, const SdfCollider2D& sdf, bool movable,
                         std::vector<ColliderItem2D>& out) {
    for (const glm::ivec2 chunk : sdf.field.chunk_coords()) {
        if (sdf_chunk_has_part_2d(chunk)) {
            out.push_back(make_sdf_item_2d(entity, matrix, sdf, chunk, movable));
        }
    }
}

bool has_collider_2d(const ecs::World& world, ecs::Entity entity) {
    return world.try_get<Transform>(entity) != nullptr &&
           (world.try_get<BoxCollider2D>(entity) != nullptr || world.try_get<CircleCollider2D>(entity) != nullptr ||
            world.try_get<PolygonCollider2D>(entity) != nullptr ||
            world.try_get<CompoundCollider2D>(entity) != nullptr || world.try_get<SdfCollider2D>(entity) != nullptr);
}

void append_colliders_2d(const ecs::World& world, ecs::Entity entity, bool movable, bool with_terrain,
                         std::vector<ColliderItem2D>& out) {
    const Transform* transform = world.try_get<Transform>(entity);
    if (transform == nullptr) {
        return;
    }
    const BoxCollider2D* box = world.try_get<BoxCollider2D>(entity);
    const CircleCollider2D* circle = world.try_get<CircleCollider2D>(entity);
    const PolygonCollider2D* polygon = world.try_get<PolygonCollider2D>(entity);
    const CompoundCollider2D* compound = with_terrain ? world.try_get<CompoundCollider2D>(entity) : nullptr;
    const SdfCollider2D* sdf = with_terrain ? world.try_get<SdfCollider2D>(entity) : nullptr;
    if (box == nullptr && circle == nullptr && polygon == nullptr && compound == nullptr && sdf == nullptr) {
        return;
    }
    const glm::mat4 matrix = collider_world_matrix_2d(world, entity, *transform);
    if (box != nullptr) {
        out.push_back(make_box_item_2d(entity, matrix, *box, movable));
    }
    if (circle != nullptr) {
        out.push_back(make_circle_item_2d(entity, matrix, *circle, movable));
    }
    if (polygon != nullptr) {
        out.push_back(make_polygon_item_2d(entity, matrix, *polygon, movable));
    }
    if (compound != nullptr) {
        const auto count = static_cast<std::uint32_t>(compound->shapes.size());
        for (std::uint32_t part = 0; part < count; ++part) {
            out.push_back(make_compound_item_2d(entity, matrix, *compound, part, movable));
        }
    }
    if (sdf != nullptr) {
        append_sdf_items_2d(entity, matrix, *sdf, movable, out);
    }
}

std::vector<ColliderItem2D> gather_active_colliders_2d(ecs::World& world, std::vector<ecs::Entity>& movable_entities) {
    struct Visit {
        ecs::Entity entity{};
        bool dynamic{false};  // the nearest RigidBody2D is Dynamic
    };
    std::vector<Visit> stack;
    world.view<RigidBody2D>().each([&](ecs::Entity entity, const RigidBody2D& body) {
        if (body.type == BodyType::Dynamic || body.type == BodyType::Kinematic) {
            stack.push_back(Visit{entity, body.type == BodyType::Dynamic});
        }
    });

    // A descendant follows the nearest RigidBody2D on it or above it, so the walk stops at a child with its own body.
    std::vector<ColliderItem2D> active;
    movable_entities.clear();
    while (!stack.empty()) {
        const Visit visit = stack.back();
        const ecs::Entity entity = visit.entity;
        stack.pop_back();
        if (has_collider_2d(world, entity)) {
            movable_entities.push_back(entity);
        }
        append_colliders_2d(world, entity, true, !visit.dynamic, active);
        if (visit.dynamic) {
            warn_dynamic_terrain(world, entity);
        }
        const Children* children = world.try_get<Children>(entity);
        if (children == nullptr) {
            continue;
        }
        for (ecs::Entity child : children->entities) {
            const Parent* parent = world.try_get<Parent>(child);
            if (!world.valid(child) || parent == nullptr || parent->entity != entity ||
                world.try_get<RigidBody2D>(child) != nullptr) {
                continue;
            }
            stack.push_back(Visit{child, visit.dynamic});
        }
    }
    std::sort(active.begin(), active.end(), collider_order_less_2d);
    std::sort(movable_entities.begin(), movable_entities.end());
    movable_entities.erase(std::unique(movable_entities.begin(), movable_entities.end()), movable_entities.end());
    return active;
}

void refresh_collider_poses_2d(const ecs::World& world, std::span<ColliderItem2D> items) {
    for (ColliderItem2D& item : items) {
        const glm::mat4 matrix = ecs::world_matrix(world, item.entity);
        item.position = glm::vec3(matrix[3]);
        item.angle = yaw_of(matrix);
    }
}

}
