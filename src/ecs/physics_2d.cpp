#include <engine/ecs/physics_2d.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>

#include "contact_2d.h"
#include "solver_2d.h"

#include <glm/vec4.hpp>

#include <algorithm>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace {

struct PhysicsOverlapState {
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> last{};  // value: was_trigger
};

enum class ColliderShape {
    Box,
    Circle,
};

struct ColliderItem {
    ecs::Entity entity{};
    glm::vec3 position{0};
    ColliderShape shape{ColliderShape::Box};
    glm::vec3 box_size{1.f, 1.f, 1.f};
    float radius{0.5f};
    uint32_t layer{1};
    uint32_t mask{0xFFFFFFFFu};
    bool is_trigger{false};
};

struct DynamicTarget {
    ecs::Entity entity{};
    int depth{0};
    glm::vec3 position{0};
};

struct SolverSlot {
    ecs::Entity entity{};
    SolverBody2D body{};
    int depth{0};
};

[[nodiscard]] std::pair<ecs::Entity, ecs::Entity> make_pair(ecs::Entity a, ecs::Entity b) {
    if (b < a) {
        return {b, a};
    }
    return {a, b};
}

[[nodiscard]] bool layers_match(const ColliderItem& a, const ColliderItem& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

[[nodiscard]] ContactShape2D to_shape(const ColliderItem& item) {
    ContactShape2D shape;
    shape.center = glm::vec2{item.position.x, item.position.y};
    if (item.shape == ColliderShape::Box) {
        shape.kind = ContactShapeKind::Aabb;
        shape.half_extents = glm::vec2{item.box_size.x * 0.5f, item.box_size.y * 0.5f};
    } else {
        shape.kind = ContactShapeKind::Circle;
        shape.radius = item.radius;
    }
    return shape;
}

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

// Root: local xy is world xy (the body's own rotation and scale are not applied).
// Child: the parent's world linear map (rotation, scale, skew) times the local velocity, then XY.
// That is the displacement rate `local.position += velocity * dt` produces under model_matrix = T * R * S.
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

void integrate_dynamics(ecs::World& world, float dt) {
    const glm::vec3 gravity = world.ctx<Physics2DConfig>().gravity;
    std::vector<ecs::Entity> dynamics;
    world.view<Transform, RigidBody2D>().each([&](ecs::Entity entity, const Transform&, const RigidBody2D& body) {
        if (body.type == BodyType2D::Dynamic) {
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
}

std::vector<ColliderItem> gather_colliders(ecs::World& world) {
    std::vector<ColliderItem> colliders;
    world.view<Transform, BoxCollider2D>().each(
            [&](ecs::Entity entity, const Transform&, const BoxCollider2D& collider) {
                colliders.push_back(ColliderItem{
                        .entity = entity,
                        .position = ecs::world_position(world, entity),
                        .shape = ColliderShape::Box,
                        .box_size = collider.size,
                        .layer = collider.layer,
                        .mask = collider.mask,
                        .is_trigger = collider.is_trigger,
                });
            });
    world.view<Transform, CircleCollider2D>().each(
            [&](ecs::Entity entity, const Transform&, const CircleCollider2D& collider) {
                colliders.push_back(ColliderItem{
                        .entity = entity,
                        .position = ecs::world_position(world, entity),
                        .shape = ColliderShape::Circle,
                        .radius = collider.radius,
                        .layer = collider.layer,
                        .mask = collider.mask,
                        .is_trigger = collider.is_trigger,
                });
            });
    return colliders;
}

void publish_events(ecs::World& world, const std::vector<ColliderItem>& colliders) {
    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> current;
    ecs::EventWriter<CollisionEvent2D> writer{world};

    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const ColliderItem& a = colliders[i];
            const ColliderItem& b = colliders[j];
            if (!layers_match(a, b)) {
                continue;
            }
            const auto pair = make_pair(a.entity, b.entity);
            const ColliderItem& first = pair.first == a.entity ? a : b;
            const ColliderItem& second = pair.first == a.entity ? b : a;
            const ContactManifold2D manifold = make_contact_2d(to_shape(first), to_shape(second));
            if (manifold.count == 0) {
                continue;
            }
            const bool is_trigger = a.is_trigger || b.is_trigger;
            current.emplace(pair, is_trigger);
            const CollisionPhase phase = state.last.contains(pair) ? CollisionPhase::Stay : CollisionPhase::Enter;
            writer.send(CollisionEvent2D{
                    .a = pair.first,
                    .b = pair.second,
                    .phase = phase,
                    .is_trigger = is_trigger,
                    .normal = manifold.normal,
                    .penetration = manifold.points[0].penetration,
                    .point = manifold.points[0].point,
            });
        }
    }

    for (const auto& [pair, was_trigger] : state.last) {
        if (!current.contains(pair)) {
            writer.send(CollisionEvent2D{pair.first, pair.second, CollisionPhase::Exit, was_trigger});
        }
    }

    state.last = std::move(current);
}

struct ItemPair {
    int item_a{0};
    int item_b{0};
};

void solve_contacts(ecs::World& world, const std::vector<ColliderItem>& colliders) {
    std::vector<ItemPair> item_pairs;
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const ColliderItem& a = colliders[i];
            const ColliderItem& b = colliders[j];
            if (a.is_trigger || b.is_trigger || !layers_match(a, b) || a.entity == b.entity) {
                continue;
            }
            const bool dynamic_a = is_dynamic(world.try_get<RigidBody2D>(a.entity));
            const bool dynamic_b = is_dynamic(world.try_get<RigidBody2D>(b.entity));
            if (!dynamic_a && !dynamic_b) {
                continue;
            }
            if (a.entity < b.entity) {
                item_pairs.push_back(ItemPair{static_cast<int>(i), static_cast<int>(j)});
            } else {
                item_pairs.push_back(ItemPair{static_cast<int>(j), static_cast<int>(i)});
            }
        }
    }
    if (item_pairs.empty()) {
        return;
    }

    std::sort(item_pairs.begin(), item_pairs.end(), [&](const ItemPair& lhs, const ItemPair& rhs) {
        const ColliderItem& left_a = colliders[static_cast<std::size_t>(lhs.item_a)];
        const ColliderItem& left_b = colliders[static_cast<std::size_t>(lhs.item_b)];
        const ColliderItem& right_a = colliders[static_cast<std::size_t>(rhs.item_a)];
        const ColliderItem& right_b = colliders[static_cast<std::size_t>(rhs.item_b)];
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
            slot.body.velocity = {rigid->velocity.x, rigid->velocity.y};
        } else if (rigid != nullptr && rigid->type == BodyType2D::Kinematic) {
            slot.body.velocity = kinematic_world_velocity_xy(world, entity, rigid->velocity);
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
        const ColliderItem& collider = colliders[index];
        SolverShape2D shape;
        shape.body = body_for(collider.entity);
        if (collider.shape == ColliderShape::Box) {
            shape.kind = ContactShapeKind::Aabb;
            shape.half_extents = {collider.box_size.x * 0.5f, collider.box_size.y * 0.5f};
        } else {
            shape.kind = ContactShapeKind::Circle;
            shape.radius = collider.radius;
        }
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
    for (int iteration = 0; iteration < kPositionIterations2D; ++iteration) {
        ecs::update_world_transforms(world);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const glm::vec3 position = ecs::world_position(world, slots[i].entity);
            bodies[i].position = {position.x, position.y};
            world_z[i] = position.z;
        }
        solve_position_iteration_2d(bodies, shapes, pairs);
        for (int index : write_order) {
            const std::size_t slot = static_cast<std::size_t>(index);
            if (!bodies[slot].dynamic) {
                continue;
            }
            ecs::set_world_position(world, slots[slot].entity,
                                    glm::vec3{bodies[slot].position.x, bodies[slot].position.y, world_z[slot]});
        }
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!bodies[i].dynamic) {
            continue;
        }
        RigidBody2D& rigid = world.get<RigidBody2D>(slots[i].entity);
        rigid.velocity.x = bodies[i].velocity.x;
        rigid.velocity.y = bodies[i].velocity.y;
    }
}

}

void run_physics_2d(ecs::World& world) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    world.view<Transform, RigidBody2D>().each([dt](Transform& transform, const RigidBody2D& body) {
        if (body.type != BodyType2D::Kinematic) {
            return;
        }
        transform.position += body.velocity * dt;
    });
    ecs::update_world_transforms(world);

    integrate_dynamics(world, dt);
    ecs::update_world_transforms(world);

    // Events use this pose (pre-solve). The solver runs after publish_events and must not change their order.
    const std::vector<ColliderItem> colliders = gather_colliders(world);
    publish_events(world, colliders);
    solve_contacts(world, colliders);
    ecs::update_world_transforms(world);
}

}
