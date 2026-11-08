#include "physics_2d_internal.h"
#include "physics_owner.h"

#include <engine/ecs/events.h>

#include <algorithm>
#include <span>
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

[[nodiscard]] bool is_movable_2d(const ecs::World& world, ecs::Entity entity) {
    return has_movable_body_2d(world, entity);
}

}

void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                                 std::span<const std::pair<int, int>> candidate_pairs) {
    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    state.current.clear();
    if (state.current.capacity() < state.last.size()) {
        state.current.reserve(state.last.size());
    }
    ecs::EventWriter<CollisionEvent2D> writer{world};

    for (const auto& [index_a, index_b] : candidate_pairs) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || claimed_by_physics_3d(world, a.entity) ||
            claimed_by_physics_3d(world, b.entity) || !layers_match_2d(a, b)) {
            continue;
        }
        const bool movable_a = is_movable_2d(world, a.entity);
        const bool movable_b = is_movable_2d(world, b.entity);
        if (!movable_a && !movable_b && !a.is_trigger && !b.is_trigger) {
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
        const auto it = std::ranges::lower_bound(state.current, pair, {}, &OverlapRecord2D::pair);
        if (it != state.current.end() && it->pair == pair) {
            continue;
        }
        state.current.insert(it, OverlapRecord2D{.pair = pair, .is_trigger = is_trigger});

        const auto last_it = std::ranges::lower_bound(state.last, pair, {}, &OverlapRecord2D::pair);
        const bool was_in_last = (last_it != state.last.end() && last_it->pair == pair);
        const CollisionPhase phase = was_in_last ? CollisionPhase::Stay : CollisionPhase::Enter;
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

    for (const auto& [last_pair, was_trigger] : state.last) {
        const auto curr_it = std::ranges::lower_bound(state.current, last_pair, {}, &OverlapRecord2D::pair);
        if (curr_it == state.current.end() || curr_it->pair != last_pair) {
            writer.send(CollisionEvent2D{
                    .a = last_pair.first,
                    .b = last_pair.second,
                    .phase = CollisionPhase::Exit,
                    .is_trigger = was_trigger,
            });
        }
    }
    state.last.swap(state.current);
}

}
