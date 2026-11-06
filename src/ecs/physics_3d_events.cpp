#include "physics_3d_internal.h"

#include <engine/ecs/events.h>

#include <algorithm>
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

[[nodiscard]] std::vector<std::pair<int, int>> candidate_pairs(const std::vector<ColliderItem3D>& colliders) {
    std::vector<Aabb> boxes;
    boxes.reserve(colliders.size());
    for (const ColliderItem3D& item : colliders) {
        boxes.push_back(aabb_of_3d(item));
    }
    Broadphase tree;
    tree.rebuild(boxes);
    std::vector<std::pair<int, int>> pairs;
    tree.query_pairs(pairs);
    std::sort(pairs.begin(), pairs.end());
    return pairs;
}

}

void publish_collision_events_3d(ecs::World& world, const std::vector<ColliderItem3D>& colliders) {
    PhysicsOverlapState3D& state = world.ctx<PhysicsOverlapState3D>();
    std::map<std::pair<ecs::Entity, ecs::Entity>, bool> current;
    ecs::EventWriter<CollisionEvent3D> writer{world};

    for (const auto& [index_a, index_b] : candidate_pairs(colliders)) {
        const ColliderItem3D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem3D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || !layers_match_3d(a, b)) {
            continue;
        }
        const auto pair = ordered_pair(a.entity, b.entity);
        const ColliderItem3D& first = pair.first == a.entity ? a : b;
        const ColliderItem3D& second = pair.first == a.entity ? b : a;
        const ContactManifold3D manifold = make_contact_3d(contact_shape_of_3d(first), contact_shape_of_3d(second));
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

    for (const auto& [pair, was_trigger] : state.last) {
        if (!current.contains(pair)) {
            writer.send(CollisionEvent3D{pair.first, pair.second, CollisionPhase::Exit, was_trigger});
        }
    }
    state.last = std::move(current);
}

}
