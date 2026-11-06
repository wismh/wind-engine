#include "physics_2d_internal.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/transform.h>

#include "broadphase.h"
#include "contact_islands.h"
#include "inertia_2d.h"
#include "solver_2d.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace {

struct SolverSlot {
    ecs::Entity entity{};
    SolverBody2D body{};
    int depth{0};
    bool frozen{false};
};

struct ItemPair {
    int item_a{0};
    int item_b{0};
};

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

// Heading of the world matrix X axis in XY. Same epsilon as angle_z. Scale changes the length, not the heading.
[[nodiscard]] float world_yaw(const ecs::World& world, ecs::Entity entity) {
    const glm::mat4 matrix = ecs::compute_world_matrix(world, entity);
    const float x = matrix[0][0];
    const float y = matrix[0][1];
    if (std::abs(x) < 1.0e-6f && std::abs(y) < 1.0e-6f) {
        return 0.f;
    }
    return std::atan2(y, x);
}

// Root: local xy is world xy (the body's own rotation and scale are not applied).
// Child: the parent's world linear map (rotation, scale, skew) times the local velocity, then XY.
[[nodiscard]] glm::vec2 kinematic_world_velocity_xy(const ecs::World& world, ecs::Entity entity, glm::vec3 velocity) {
    const Parent* parent = world.try_get<Parent>(entity);
    if (parent == nullptr || !world.valid(parent->entity)) {
        return {velocity.x, velocity.y};
    }
    const glm::vec4 world_velocity = ecs::world_matrix(world, parent->entity) * glm::vec4(velocity, 0.f);
    return {world_velocity.x, world_velocity.y};
}

[[nodiscard]] float dynamic_inverse_mass(const RigidBody2D& body) {
    const float mass = body.mass > kMinDynamicMass ? body.mass : kMinDynamicMass;
    return 1.f / mass;
}

[[nodiscard]] bool is_dynamic(const RigidBody2D* body) {
    return body != nullptr && body->type == BodyType2D::Dynamic;
}

[[nodiscard]] float dynamic_mass(const RigidBody2D& body) {
    return body.mass > kMinDynamicMass ? body.mass : kMinDynamicMass;
}

// First non-trigger collider in gather order (boxes, then circles, then polygons). One mass, not a sum.
[[nodiscard]] float inverse_inertia_for(const ecs::World& world, ecs::Entity entity,
                                        const std::vector<ColliderItem2D>& colliders) {
    const RigidBody2D* rigid = world.try_get<RigidBody2D>(entity);
    if (!is_dynamic(rigid)) {
        return 0.f;
    }
    const float mass = dynamic_mass(*rigid);
    for (const ColliderItem2D& item : colliders) {
        if (item.entity != entity || item.is_trigger) {
            continue;
        }
        float inertia = 0.f;
        if (item.shape == ColliderShape2D::Box) {
            inertia = inertia_2d_box(mass, item.box_size.x, item.box_size.y);
        } else if (item.shape == ColliderShape2D::Circle) {
            inertia = inertia_2d_circle(mass, item.radius);
        } else {
            inertia = inertia_2d_polygon(mass, item.polygon, item.polygon_count);
        }
        return inertia > 0.f ? 1.f / inertia : 0.f;
    }
    return 0.f;
}

void fill_solver_shape(SolverShape2D& shape, const ColliderItem2D& collider) {
    shape.angle = collider.angle;
    shape.friction = collider.friction;
    shape.restitution = collider.restitution;
    if (collider.shape == ColliderShape2D::Box) {
        shape.kind = ContactShapeKind::Aabb;
        shape.half_extents = {collider.box_size.x * 0.5f, collider.box_size.y * 0.5f};
    } else if (collider.shape == ColliderShape2D::Circle) {
        shape.kind = ContactShapeKind::Circle;
        shape.radius = collider.radius;
    } else {
        shape.kind = ContactShapeKind::Polygon;
        shape.vertex_count = std::min(collider.polygon_count, kPolygonCollider2DCapacity);
        for (int i = 0; i < shape.vertex_count; ++i) {
            shape.vertices[i] = collider.polygon[i];
        }
    }
}

}

std::vector<ColliderItem2D> gather_colliders_2d(ecs::World& world) {
    std::vector<ColliderItem2D> colliders;
    world.view<Transform, BoxCollider2D>().each([&](ecs::Entity entity, const Transform&, const BoxCollider2D& box) {
        colliders.push_back(ColliderItem2D{
                .entity = entity,
                .position = ecs::world_position(world, entity),
                .angle = world_yaw(world, entity),
                .shape = ColliderShape2D::Box,
                .box_size = box.size,
                .layer = box.layer,
                .mask = box.mask,
                .is_trigger = box.is_trigger,
                .friction = box.friction,
                .restitution = box.restitution,
        });
    });
    world.view<Transform, CircleCollider2D>().each(
            [&](ecs::Entity entity, const Transform&, const CircleCollider2D& circle) {
                colliders.push_back(ColliderItem2D{
                        .entity = entity,
                        .position = ecs::world_position(world, entity),
                        .angle = world_yaw(world, entity),
                        .shape = ColliderShape2D::Circle,
                        .radius = circle.radius,
                        .layer = circle.layer,
                        .mask = circle.mask,
                        .is_trigger = circle.is_trigger,
                        .friction = circle.friction,
                        .restitution = circle.restitution,
                });
            });
    world.view<Transform, PolygonCollider2D>().each(
            [&](ecs::Entity entity, const Transform&, const PolygonCollider2D& polygon) {
                ColliderItem2D item{
                        .entity = entity,
                        .position = ecs::world_position(world, entity),
                        .angle = world_yaw(world, entity),
                        .shape = ColliderShape2D::Polygon,
                        .layer = polygon.layer,
                        .mask = polygon.mask,
                        .is_trigger = polygon.is_trigger,
                        .friction = polygon.friction,
                        .restitution = polygon.restitution,
                };
                item.polygon_count = std::min(static_cast<int>(polygon.count), kPolygonCollider2DCapacity);
                for (int i = 0; i < item.polygon_count; ++i) {
                    item.polygon[i] = polygon.vertices[i];
                }
                colliders.push_back(item);
            });
    return colliders;
}

void solve_physics_contacts_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders) {
    std::vector<Aabb> bounds(colliders.size());
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        bounds[i] = aabb_of_2d(colliders[i]);
    }
    Broadphase tree;
    tree.rebuild(bounds);
    std::vector<std::pair<int, int>> candidates;
    tree.query_pairs(candidates);

    std::vector<ItemPair> item_pairs;
    for (const auto& [index_a, index_b] : candidates) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (world.try_get<RigidBody3D>(a.entity) != nullptr || world.try_get<RigidBody3D>(b.entity) != nullptr) {
            continue;
        }
        if (a.is_trigger || b.is_trigger || !layers_match_2d(a, b) || a.entity == b.entity) {
            continue;
        }
        const bool dynamic_a = is_dynamic(world.try_get<RigidBody2D>(a.entity));
        const bool dynamic_b = is_dynamic(world.try_get<RigidBody2D>(b.entity));
        if (!dynamic_a && !dynamic_b) {
            continue;
        }
        if (a.entity < b.entity) {
            item_pairs.push_back(ItemPair{index_a, index_b});
        } else {
            item_pairs.push_back(ItemPair{index_b, index_a});
        }
    }
    if (item_pairs.empty()) {
        world.ctx<PhysicsContactCache2D>().impulses.clear();
        update_sleep_2d(world, {});
        return;
    }

    std::sort(item_pairs.begin(), item_pairs.end(), [&](const ItemPair& lhs, const ItemPair& rhs) {
        const ColliderItem2D& left_a = colliders[static_cast<std::size_t>(lhs.item_a)];
        const ColliderItem2D& left_b = colliders[static_cast<std::size_t>(lhs.item_b)];
        const ColliderItem2D& right_a = colliders[static_cast<std::size_t>(rhs.item_a)];
        const ColliderItem2D& right_b = colliders[static_cast<std::size_t>(rhs.item_b)];
        if (left_a.entity != right_a.entity) {
            return left_a.entity < right_a.entity;
        }
        if (left_b.entity != right_b.entity) {
            return left_b.entity < right_b.entity;
        }
        if (left_a.shape != right_a.shape) {
            return static_cast<int>(left_a.shape) < static_cast<int>(right_a.shape);
        }
        if (left_b.shape != right_b.shape) {
            return static_cast<int>(left_b.shape) < static_cast<int>(right_b.shape);
        }
        if (lhs.item_a != rhs.item_a) {
            return lhs.item_a < rhs.item_a;
        }
        return lhs.item_b < rhs.item_b;
    });

    std::unordered_map<ecs::Entity, int> body_of;
    std::vector<SolverSlot> slots;
    auto body_for = [&](ecs::Entity entity) {
        const auto found = body_of.find(entity);
        if (found != body_of.end()) {
            return found->second;
        }
        const int index = static_cast<int>(slots.size());
        body_of.emplace(entity, index);
        SolverSlot slot;
        slot.entity = entity;
        slot.depth = hierarchy_depth(world, entity);
        const RigidBody2D* rigid = world.try_get<RigidBody2D>(entity);
        if (is_dynamic(rigid)) {
            slot.body.dynamic = true;
            slot.body.inverse_mass = dynamic_inverse_mass(*rigid);
            slot.body.inverse_inertia = inverse_inertia_for(world, entity, colliders);
            slot.body.velocity = {rigid->velocity.x, rigid->velocity.y};
            slot.body.angular_velocity = rigid->angular_velocity;
            if (rigid->sleeping) {
                slot.frozen = true;
                slot.body.inverse_mass = 0.f;
                slot.body.inverse_inertia = 0.f;
                slot.body.velocity = {0.f, 0.f};
                slot.body.angular_velocity = 0.f;
            }
        } else if (rigid != nullptr && rigid->type == BodyType2D::Kinematic) {
            slot.body.velocity = kinematic_world_velocity_xy(world, entity, rigid->velocity);
            slot.body.angular_velocity = rigid->angular_velocity;
        } else if (rigid != nullptr) {
            slot.body.angular_velocity = rigid->angular_velocity;
        }
        slots.push_back(slot);
        return index;
    };

    std::vector<int> shape_of(colliders.size(), -1);
    std::vector<SolverShape2D> shapes;
    auto shape_for = [&](int item) {
        const std::size_t index = static_cast<std::size_t>(item);
        if (shape_of[index] >= 0) {
            return shape_of[index];
        }
        SolverShape2D shape;
        shape.body = body_for(colliders[index].entity);
        fill_solver_shape(shape, colliders[index]);
        shape_of[index] = static_cast<int>(shapes.size());
        shapes.push_back(shape);
        return shape_of[index];
    };

    std::vector<SolverPair2D> pairs;
    pairs.reserve(item_pairs.size());
    for (const ItemPair& item : item_pairs) {
        pairs.push_back(SolverPair2D{shape_for(item.item_a), shape_for(item.item_b)});
    }

    std::vector<SolverBody2D> bodies;
    bodies.reserve(slots.size());
    for (const SolverSlot& slot : slots) {
        bodies.push_back(slot.body);
    }

    std::vector<int> write_order(slots.size());
    std::iota(write_order.begin(), write_order.end(), 0);
    std::stable_sort(write_order.begin(), write_order.end(), [&](int a, int b) {
        return slots[static_cast<std::size_t>(a)].depth < slots[static_cast<std::size_t>(b)].depth;
    });

    std::vector<float> world_z(slots.size(), 0.f);
    auto refresh_bodies = [&] {
        ecs::update_world_transforms(world);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const glm::vec3 position = ecs::world_position(world, slots[i].entity);
            bodies[i].position = {position.x, position.y};
            world_z[i] = position.z;
        }
    };
    auto cache_key_for = [&](const SolverPair2D& pair) {
        const SolverShape2D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape2D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        ContactCacheKey2D key;
        key.a = slots[static_cast<std::size_t>(shape_a.body)].entity;
        key.b = slots[static_cast<std::size_t>(shape_b.body)].entity;
        key.shape_a = static_cast<std::uint8_t>(shape_a.kind);
        key.shape_b = static_cast<std::uint8_t>(shape_b.kind);
        return key;
    };

    std::vector<ecs::Entity> entities;
    entities.reserve(slots.size());
    for (const SolverSlot& slot : slots) {
        entities.push_back(slot.entity);
    }
    std::vector<IslandBodyPair> links(pairs.size());
    std::vector<std::uint8_t> overlapping(pairs.size(), 0);
    refresh_bodies();
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const SolverShape2D& shape_a = shapes[static_cast<std::size_t>(pairs[i].shape_a)];
        const SolverShape2D& shape_b = shapes[static_cast<std::size_t>(pairs[i].shape_b)];
        links[i] = IslandBodyPair{shape_a.body, shape_b.body};
        if (shape_a.body == shape_b.body) {
            continue;
        }
        const ContactManifold2D manifold =
                make_contact_2d(contact_shape_2d(shape_a, bodies[static_cast<std::size_t>(shape_a.body)].position),
                                contact_shape_2d(shape_b, bodies[static_cast<std::size_t>(shape_b.body)].position));
        if (manifold.count > 0) {
            overlapping[i] = 1;
        }
    }
    const std::vector<std::vector<int>> islands = group_contact_islands(entities, links, overlapping);
    if (islands.empty()) {
        world.ctx<PhysicsContactCache2D>().impulses.clear();
        update_sleep_2d(world, {});
        return;
    }
    std::vector<SolverPair2D> ordered;
    std::vector<std::size_t> group_begin;
    std::vector<std::size_t> group_count;
    ordered.reserve(pairs.size());
    for (const std::vector<int>& island : islands) {
        group_begin.push_back(ordered.size());
        for (int index : island) {
            ordered.push_back(pairs[static_cast<std::size_t>(index)]);
        }
        group_count.push_back(ordered.size() - group_begin.back());
    }
    pairs = std::move(ordered);

    // Seed the accumulator only. Do not apply the cached impulse to velocity. A flipped normal drops both points.
    PhysicsContactCache2D& cache = world.ctx<PhysicsContactCache2D>();
    std::vector<ContactImpulse2D> impulses(pairs.size());
    refresh_bodies();
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const SolverPair2D& pair = pairs[i];
        const SolverShape2D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape2D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        if (shape_a.body == shape_b.body) {
            continue;
        }
        const auto found = cache.impulses.find(cache_key_for(pair));
        if (found == cache.impulses.end()) {
            continue;
        }
        const std::size_t body_a = static_cast<std::size_t>(shape_a.body);
        const std::size_t body_b = static_cast<std::size_t>(shape_b.body);
        const ContactManifold2D manifold =
                make_contact_2d(contact_shape_2d(shape_a, bodies[body_a].position),
                                contact_shape_2d(shape_b, bodies[body_b].position));
        if (manifold.count == 0 || glm::dot(found->second.normal_axis, manifold.normal) <= 0.f) {
            continue;
        }
        const int seeded = std::min(manifold.count, kContact2DPointCapacity);
        for (int point = 0; point < seeded; ++point) {
            impulses[i].points[point].normal = found->second.normal[point];
            impulses[i].points[point].tangent = found->second.tangent[point];
        }
    }

    for (std::size_t group = 0; group < group_begin.size(); ++group) {
        const std::span<const SolverPair2D> pair_span{pairs.data() + group_begin[group], group_count[group]};
        const std::span<ContactImpulse2D> impulse_span{impulses.data() + group_begin[group], group_count[group]};
        for (int iteration = 0; iteration < kPositionIterations2D; ++iteration) {
            refresh_bodies();
            solve_position_iteration_2d(bodies, shapes, pair_span, impulse_span);
            for (int index : write_order) {
                const std::size_t slot = static_cast<std::size_t>(index);
                if (!bodies[slot].dynamic || slots[slot].frozen) {
                    continue;
                }
                ecs::set_world_position(world, slots[slot].entity,
                                        glm::vec3{bodies[slot].position.x, bodies[slot].position.y, world_z[slot]});
            }
        }
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!bodies[i].dynamic || slots[i].frozen) {
            continue;
        }
        RigidBody2D& rigid = world.get<RigidBody2D>(slots[i].entity);
        rigid.velocity.x = bodies[i].velocity.x;
        rigid.velocity.y = bodies[i].velocity.y;
        rigid.angular_velocity = bodies[i].angular_velocity;
    }

    PhysicsContactCache2D rebuilt;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (!impulses[i].touched) {
            continue;
        }
        PhysicsContactCache2D::Impulse stored;
        for (int point = 0; point < kContact2DPointCapacity; ++point) {
            stored.normal[point] = impulses[i].points[point].normal;
            stored.tangent[point] = impulses[i].points[point].tangent;
        }
        stored.normal_axis = impulses[i].normal_axis;
        rebuilt.impulses.emplace(cache_key_for(pairs[i]), stored);
    }
    cache = std::move(rebuilt);

    std::vector<ecs::Entity> contacting;
    contacting.reserve(pairs.size() * 2);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (!impulses[i].touched) {
            continue;
        }
        const SolverShape2D& shape_a = shapes[static_cast<std::size_t>(pairs[i].shape_a)];
        const SolverShape2D& shape_b = shapes[static_cast<std::size_t>(pairs[i].shape_b)];
        contacting.push_back(slots[static_cast<std::size_t>(shape_a.body)].entity);
        contacting.push_back(slots[static_cast<std::size_t>(shape_b.body)].entity);
    }
    update_sleep_2d(world, contacting);
}

}
