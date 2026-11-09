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

}

void publish_collision_events_2d(ecs::World& world, const std::vector<ColliderItem2D>& colliders,
                                 std::span<const std::pair<int, int>> candidate_pairs) {
    PhysicsOverlapState& state = world.ctx<PhysicsOverlapState>();
    state.current.clear();
    state.pending.clear();
    if (state.current.capacity() < state.last.size()) {
        state.current.reserve(state.last.size());
    }
    ecs::EventWriter<CollisionEvent2D> writer{world};

    // One event per entity pair, in the order of its first shape pair. Several shape pairs of the same two entities
    // (a compound, or several colliders on one entity) publish the deepest; equal depth keeps the earlier.

    for (const auto& [index_a, index_b] : candidate_pairs) {
        const ColliderItem2D& a = colliders[static_cast<std::size_t>(index_a)];
        const ColliderItem2D& b = colliders[static_cast<std::size_t>(index_b)];
        if (a.entity == b.entity || claimed_by_physics_3d(world, a.entity) ||
            claimed_by_physics_3d(world, b.entity) || !layers_match_2d(a, b)) {
            continue;
        }
        if (!a.movable && !b.movable && !a.is_trigger && !b.is_trigger) {
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
            CollisionEvent2D& event = state.pending[static_cast<std::size_t>(it->event)];
            if (manifold.points[0].penetration > event.penetration) {
                event.is_trigger = is_trigger;
                event.normal = manifold.normal;
                event.penetration = manifold.points[0].penetration;
                event.point = manifold.points[0].point;
                it->is_trigger = is_trigger;
            }
            continue;
        }
        const int event_index = static_cast<int>(state.pending.size());
        state.current.insert(it, OverlapRecord2D{.pair = pair, .is_trigger = is_trigger, .event = event_index});

        const auto last_it = std::ranges::lower_bound(state.last, pair, {}, &OverlapRecord2D::pair);
        const bool was_in_last = (last_it != state.last.end() && last_it->pair == pair);
        const CollisionPhase phase = was_in_last ? CollisionPhase::Stay : CollisionPhase::Enter;
        state.pending.push_back(CollisionEvent2D{
                .a = pair.first,
                .b = pair.second,
                .phase = phase,
                .is_trigger = is_trigger,
                .normal = manifold.normal,
                .penetration = manifold.points[0].penetration,
                .point = manifold.points[0].point,
        });
    }
    for (const CollisionEvent2D& event : state.pending) {
        writer.send(event);
    }

    for (const OverlapRecord2D& last : state.last) {
        const auto curr_it = std::ranges::lower_bound(state.current, last.pair, {}, &OverlapRecord2D::pair);
        if (curr_it == state.current.end() || curr_it->pair != last.pair) {
            writer.send(CollisionEvent2D{
                    .a = last.pair.first,
                    .b = last.pair.second,
                    .phase = CollisionPhase::Exit,
                    .is_trigger = last.is_trigger,
            });
        }
    }
    state.last.swap(state.current);
}

}
