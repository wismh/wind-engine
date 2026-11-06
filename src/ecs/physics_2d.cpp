#include <engine/ecs/physics_2d.h>

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>

#include "contact_2d.h"

#include <map>
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

}

void run_physics_2d(ecs::World& world) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    world.view<Transform, RigidBody2D>().each([dt](Transform& transform, const RigidBody2D& body) {
        transform.position += body.velocity * dt;
    });

    // Colliders sit in world space. RigidBody2D integrated the local position above (a child's velocity is in its
    // parent's space), so refresh the world matrices before reading them.
    ecs::update_world_transforms(world);

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

}
