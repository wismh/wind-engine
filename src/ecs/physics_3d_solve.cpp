#include "physics_3d_internal.h"

#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>

#include "inertia_3d.h"
#include "solver_3d.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace {

struct SolverSlot {
    ecs::Entity entity{};
    SolverBody3D body{};
    int depth{0};
};

struct ItemPair {
    int item_a{0};
    int item_b{0};
};

[[nodiscard]] std::pair<ecs::Entity, ecs::Entity> make_pair(ecs::Entity a, ecs::Entity b) {
    if (b < a) {
        return {b, a};
    }
    return {a, b};
}

[[nodiscard]] bool layers_match(const ColliderItem3D& a, const ColliderItem3D& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

[[nodiscard]] bool is_dynamic(const RigidBody3D* body) {
    return body != nullptr && body->type == BodyType3D::Dynamic;
}

[[nodiscard]] float dynamic_mass(const RigidBody3D& body) {
    return body.mass > kMinDynamicMass3D ? body.mass : kMinDynamicMass3D;
}

[[nodiscard]] float dynamic_inverse_mass(const RigidBody3D& body) {
    return 1.f / dynamic_mass(body);
}

[[nodiscard]] glm::vec3 kinematic_world_velocity(const ecs::World& world, ecs::Entity entity, glm::vec3 velocity) {
    const Parent* parent = world.try_get<Parent>(entity);
    if (parent == nullptr || !world.valid(parent->entity)) {
        return velocity;
    }
    const glm::vec4 world_velocity = ecs::world_matrix(world, parent->entity) * glm::vec4(velocity, 0.f);
    return glm::vec3{world_velocity};
}

[[nodiscard]] glm::mat3 inverse_inertia_for(const ecs::World& world, ecs::Entity entity,
                                            const std::vector<ColliderItem3D>& colliders) {
    const RigidBody3D* rigid = world.try_get<RigidBody3D>(entity);
    if (!is_dynamic(rigid)) {
        return glm::mat3{0.f};
    }
    const float mass = dynamic_mass(*rigid);
    for (const ColliderItem3D& item : colliders) {
        if (item.entity != entity || item.is_trigger) {
            continue;
        }
        glm::vec3 inertia{0.f};
        if (item.shape == ColliderShape3D::Box) {
            inertia = inertia_3d_box(mass, item.box_size);
        } else if (item.shape == ColliderShape3D::Sphere) {
            inertia = inertia_3d_sphere(mass, item.radius);
        } else {
            inertia = inertia_3d_capsule(mass, item.radius, item.height);
        }
        return inverse_inertia_world(item.rotation, inertia);
    }
    return glm::mat3{0.f};
}

[[nodiscard]] ContactShape3D to_shape(const ColliderItem3D& item) {
    ContactShape3D shape;
    shape.center = item.position;
    shape.rotation = item.rotation;
    if (item.shape == ColliderShape3D::Box) {
        shape.kind = ContactShapeKind3D::Box;
        shape.half_extents = item.box_size * 0.5f;
    } else if (item.shape == ColliderShape3D::Sphere) {
        shape.kind = ContactShapeKind3D::Sphere;
        shape.radius = item.radius;
    } else {
        shape.kind = ContactShapeKind3D::Capsule;
        shape.radius = item.radius;
        shape.height = item.height;
    }
    return shape;
}

void fill_solver_shape(SolverShape3D& shape, const ColliderItem3D& collider) {
    shape.rotation = collider.rotation;
    shape.friction = collider.friction;
    shape.restitution = collider.restitution;
    if (collider.shape == ColliderShape3D::Box) {
        shape.kind = ContactShapeKind3D::Box;
        shape.half_extents = collider.box_size * 0.5f;
    } else if (collider.shape == ColliderShape3D::Sphere) {
        shape.kind = ContactShapeKind3D::Sphere;
        shape.radius = collider.radius;
    } else {
        shape.kind = ContactShapeKind3D::Capsule;
        shape.radius = collider.radius;
        shape.height = collider.height;
    }
}

[[nodiscard]] ColliderItem3D make_item(ecs::Entity entity, glm::vec3 position, glm::quat rotation,
                                       ColliderShape3D shape, std::uint32_t layer, std::uint32_t mask, bool is_trigger,
                                       float friction, float restitution) {
    ColliderItem3D item;
    item.entity = entity;
    item.position = position;
    item.rotation = rotation;
    item.shape = shape;
    item.layer = layer;
    item.mask = mask;
    item.is_trigger = is_trigger;
    item.friction = friction;
    item.restitution = restitution;
    return item;
}

}

std::vector<ColliderItem3D> gather_colliders_3d(ecs::World& world) {
    std::vector<ColliderItem3D> colliders;
    world.view<Transform, BoxCollider3D>().each([&](ecs::Entity entity, const Transform&, const BoxCollider3D& box) {
        ColliderItem3D item =
                make_item(entity, ecs::world_position(world, entity), world_rotation_quat_3d(world, entity),
                          ColliderShape3D::Box, box.layer, box.mask, box.is_trigger, box.friction, box.restitution);
        item.box_size = box.size;
        colliders.push_back(item);
    });
    world.view<Transform, SphereCollider3D>().each(
            [&](ecs::Entity entity, const Transform&, const SphereCollider3D& sphere) {
                ColliderItem3D item = make_item(entity, ecs::world_position(world, entity),
                                                world_rotation_quat_3d(world, entity), ColliderShape3D::Sphere,
                                                sphere.layer, sphere.mask, sphere.is_trigger, sphere.friction,
                                                sphere.restitution);
                item.radius = sphere.radius;
                colliders.push_back(item);
            });
    world.view<Transform, CapsuleCollider3D>().each(
            [&](ecs::Entity entity, const Transform&, const CapsuleCollider3D& capsule) {
                ColliderItem3D item = make_item(entity, ecs::world_position(world, entity),
                                                world_rotation_quat_3d(world, entity), ColliderShape3D::Capsule,
                                                capsule.layer, capsule.mask, capsule.is_trigger, capsule.friction,
                                                capsule.restitution);
                item.radius = capsule.radius;
                item.height = capsule.height;
                colliders.push_back(item);
            });
    return colliders;
}

void publish_collision_events_3d(ecs::World& world, const std::vector<ColliderItem3D>& colliders) {
    PhysicsOverlapState3D& state = world.ctx<PhysicsOverlapState3D>();
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> current;
    ecs::EventWriter<CollisionEvent3D> writer{world};

    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const ColliderItem3D& a = colliders[i];
            const ColliderItem3D& b = colliders[j];
            if (a.entity == b.entity || !layers_match(a, b)) {
                continue;
            }
            const auto pair = make_pair(a.entity, b.entity);
            const ColliderItem3D& first = pair.first == a.entity ? a : b;
            const ColliderItem3D& second = pair.first == a.entity ? b : a;
            const ContactManifold3D manifold = make_contact_3d(to_shape(first), to_shape(second));
            if (manifold.count == 0) {
                continue;
            }
            const bool is_trigger = a.is_trigger || b.is_trigger;
            const bool inserted = current.emplace(pair, is_trigger).second;
            if (!inserted) {
                continue;
            }
            const CollisionPhase phase = state.last.contains(pair) ? CollisionPhase::Stay : CollisionPhase::Enter;
            writer.send(CollisionEvent3D{
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
            writer.send(CollisionEvent3D{pair.first, pair.second, CollisionPhase::Exit, was_trigger});
        }
    }
    state.last = std::move(current);
}

void solve_physics_contacts_3d(ecs::World& world, const std::vector<ColliderItem3D>& colliders) {
    std::vector<ItemPair> item_pairs;
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const ColliderItem3D& a = colliders[i];
            const ColliderItem3D& b = colliders[j];
            if (a.entity == b.entity || a.is_trigger || b.is_trigger || !layers_match(a, b)) {
                continue;
            }
            const bool dynamic_a = is_dynamic(world.try_get<RigidBody3D>(a.entity));
            const bool dynamic_b = is_dynamic(world.try_get<RigidBody3D>(b.entity));
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
        world.ctx<PhysicsContactCache3D>().impulses.clear();
        return;
    }

    std::sort(item_pairs.begin(), item_pairs.end(), [&](const ItemPair& lhs, const ItemPair& rhs) {
        const ColliderItem3D& left_a = colliders[static_cast<std::size_t>(lhs.item_a)];
        const ColliderItem3D& left_b = colliders[static_cast<std::size_t>(lhs.item_b)];
        const ColliderItem3D& right_a = colliders[static_cast<std::size_t>(rhs.item_a)];
        const ColliderItem3D& right_b = colliders[static_cast<std::size_t>(rhs.item_b)];
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
        slot.depth = hierarchy_depth_3d(world, entity);
        const RigidBody3D* rigid = world.try_get<RigidBody3D>(entity);
        if (is_dynamic(rigid)) {
            slot.body.dynamic = true;
            slot.body.inverse_mass = dynamic_inverse_mass(*rigid);
            slot.body.inverse_inertia = inverse_inertia_for(world, entity, colliders);
            slot.body.velocity = rigid->velocity;
            slot.body.angular_velocity = rigid->angular_velocity;
        } else if (rigid != nullptr && rigid->type == BodyType3D::Kinematic) {
            slot.body.velocity = kinematic_world_velocity(world, entity, rigid->velocity);
            slot.body.angular_velocity = rigid->angular_velocity;
        } else if (rigid != nullptr) {
            slot.body.angular_velocity = rigid->angular_velocity;
        }
        slots.push_back(slot);
        return index;
    };

    std::vector<int> shape_of(colliders.size(), -1);
    std::vector<SolverShape3D> shapes;
    auto shape_for = [&](int item) {
        const std::size_t index = static_cast<std::size_t>(item);
        if (shape_of[index] >= 0) {
            return shape_of[index];
        }
        SolverShape3D shape;
        shape.body = body_for(colliders[index].entity);
        fill_solver_shape(shape, colliders[index]);
        shape_of[index] = static_cast<int>(shapes.size());
        shapes.push_back(shape);
        return shape_of[index];
    };

    std::vector<SolverPair3D> pairs;
    pairs.reserve(item_pairs.size());
    for (const ItemPair& item : item_pairs) {
        pairs.push_back(SolverPair3D{shape_for(item.item_a), shape_for(item.item_b)});
    }

    std::vector<SolverBody3D> bodies;
    bodies.reserve(slots.size());
    for (const SolverSlot& slot : slots) {
        bodies.push_back(slot.body);
    }

    std::vector<int> write_order(slots.size());
    std::iota(write_order.begin(), write_order.end(), 0);
    std::stable_sort(write_order.begin(), write_order.end(), [&](int a, int b) {
        return slots[static_cast<std::size_t>(a)].depth < slots[static_cast<std::size_t>(b)].depth;
    });

    auto refresh_bodies = [&] {
        ecs::update_world_transforms(world);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            bodies[i].position = ecs::world_position(world, slots[i].entity);
        }
    };
    auto cache_key_for = [&](const SolverPair3D& pair) {
        const SolverShape3D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape3D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        ContactCacheKey3D key;
        key.a = slots[static_cast<std::size_t>(shape_a.body)].entity;
        key.b = slots[static_cast<std::size_t>(shape_b.body)].entity;
        key.shape_a = static_cast<std::uint8_t>(shape_a.kind);
        key.shape_b = static_cast<std::uint8_t>(shape_b.kind);
        return key;
    };

    PhysicsContactCache3D& cache = world.ctx<PhysicsContactCache3D>();
    std::vector<ContactImpulse3D> impulses(pairs.size());
    refresh_bodies();
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const SolverPair3D& pair = pairs[i];
        const SolverShape3D& shape_a = shapes[static_cast<std::size_t>(pair.shape_a)];
        const SolverShape3D& shape_b = shapes[static_cast<std::size_t>(pair.shape_b)];
        if (shape_a.body == shape_b.body) {
            continue;
        }
        const auto found = cache.impulses.find(cache_key_for(pair));
        if (found == cache.impulses.end()) {
            continue;
        }
        const std::size_t body_a = static_cast<std::size_t>(shape_a.body);
        const std::size_t body_b = static_cast<std::size_t>(shape_b.body);
        const ContactManifold3D manifold = make_contact_3d(contact_shape_3d(shape_a, bodies[body_a].position),
                                                           contact_shape_3d(shape_b, bodies[body_b].position));
        if (manifold.count == 0 || glm::dot(found->second.normal_axis, manifold.normal) <= 0.f) {
            continue;
        }
        impulses[i].point.normal = found->second.normal;
        impulses[i].point.tangent[0] = found->second.tangent[0];
        impulses[i].point.tangent[1] = found->second.tangent[1];
    }

    for (int iteration = 0; iteration < kPositionIterations3D; ++iteration) {
        refresh_bodies();
        solve_position_iteration_3d(bodies, shapes, pairs, impulses);
        for (int index : write_order) {
            const std::size_t slot = static_cast<std::size_t>(index);
            if (!bodies[slot].dynamic) {
                continue;
            }
            ecs::set_world_position(world, slots[slot].entity, bodies[slot].position);
        }
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!bodies[i].dynamic) {
            continue;
        }
        RigidBody3D& rigid = world.get<RigidBody3D>(slots[i].entity);
        rigid.velocity = bodies[i].velocity;
        rigid.angular_velocity = bodies[i].angular_velocity;
    }

    PhysicsContactCache3D rebuilt;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (!impulses[i].touched) {
            continue;
        }
        PhysicsContactCache3D::Impulse stored;
        stored.normal = impulses[i].point.normal;
        stored.tangent[0] = impulses[i].point.tangent[0];
        stored.tangent[1] = impulses[i].point.tangent[1];
        stored.normal_axis = impulses[i].normal_axis;
        rebuilt.impulses.emplace(cache_key_for(pairs[i]), stored);
    }
    cache = std::move(rebuilt);
}

}
