#include "physics_2d_internal.h"
#include "physics_owner.h"

#include <engine/ecs/events.h>

#include <utility>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] std::pair<ecs::Entity, ecs::Entity> ordered_pair(ecs::Entity a, ecs::Entity b) {
    if (b < a) {
        return {b, a};
    }
    return {a, b};
}

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem2D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem2D& item : colliders) {
        boxes.push_back(aabb_of_2d(item));
    }
    return query_sorted_pairs(boxes);
}

}

void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders) {
    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> current;
    ecs::EventWriter<CollisionEvent2D> writer{world};

    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || claimed_by_physics_3d(world, a.entity) ||
            claimed_by_physics_3d(world, b.entity) || !layers_match_2d(a, b)) {
            continue;
        }
        const auto pair = ordered_pair(a.entity, b.entity);
        const ColliderItem2D& first = pair.first == a.entity ? a : b;
        const ColliderItem2D& second = pair.first == a.entity ? b : a;
        const ContactManifold2D manifold = make_contact_2d(contact_shape_of_2d(first), contact_shape_of_2d(second));
        if (manifold.count == 0) {
            continue;
        }
        const bool is_trigger = a.is_trigger || b.is_trigger;
        const bool inserted = current.emplace(pair, is_trigger).second;
        if (!inserted) {
            continue;
        }
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

    for (const auto& [pair, was_trigger] : state.last) {
        if (!current.contains(pair)) {
            writer.send(CollisionEvent2D{pair.first, pair.second, CollisionPhase::Exit, was_trigger});
        }
    }
    state.last = std::move(current);
}

}
