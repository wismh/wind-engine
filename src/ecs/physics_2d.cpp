#include <engine/ecs/physics_2d.h>

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>

#include "broadphase.h"
#include "physics_2d_internal.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace engine {

std::vector<std::pair<int, int>> compute_broadphase_candidates_2d(
        ecs::World& world, std::span<const ColliderItem2D> active, std::vector<ColliderItem2D>& colliders) {
    PhysicsBroadphase2D& bp = world.ctx<PhysicsBroadphase2D>();

    // Active colliders and static triggers are queried every tick. Static-static pairs are never queried.
    std::vector<ColliderItem2D> queried;
    queried.reserve(active.size() + bp.static_triggers.size());
    queried.insert(queried.end(), active.begin(), active.end());
    queried.insert(queried.end(), bp.static_triggers.begin(), bp.static_triggers.end());
    colliders.clear();
    if (queried.empty()) {
        return {};
    }

    std::vector<Aabb> boxes;
    boxes.reserve(queried.size());
    for (const ColliderItem2D& item : queried) {
        boxes.push_back(aabb_of_2d(item));
    }
    bp.dynamic_tree.rebuild(boxes);
    std::vector<std::pair<int, int>> queried_pairs;
    bp.dynamic_tree.query_pairs(queried_pairs);

    // Queried item against cached static leaf.
    std::vector<std::pair<int, int>> static_hits;
    if (!bp.static_keys.empty()) {
        for (std::size_t i = 0; i < queried.size(); ++i) {
            bp.static_tree.query(boxes[i], [&](int leaf) { static_hits.emplace_back(static_cast<int>(i), leaf); });
        }
    }
    std::vector<int> touched;
    touched.reserve(static_hits.size());
    for (const auto& hit : static_hits) {
        touched.push_back(hit.second);
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());

    // Index space before the order sort: queried items, then the touched statics.
    std::vector<ColliderItem2D> unordered = std::move(queried);
    const int static_base = static_cast<int>(unordered.size());
    for (int leaf : touched) {
        unordered.push_back(bp.static_slots[static_cast<std::size_t>(leaf)].item);
    }
    auto static_index = [&](int leaf) {
        const auto it = std::lower_bound(touched.begin(), touched.end(), leaf);
        return static_base + static_cast<int>(it - touched.begin());
    };

    std::vector<int> order(unordered.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return collider_order_less_2d(unordered[static_cast<std::size_t>(a)], unordered[static_cast<std::size_t>(b)]);
    });
    std::vector<int> rank(unordered.size());
    colliders.reserve(unordered.size());
    for (std::size_t r = 0; r < order.size(); ++r) {
        rank[static_cast<std::size_t>(order[r])] = static_cast<int>(r);
        colliders.push_back(unordered[static_cast<std::size_t>(order[r])]);
    }

    std::vector<std::pair<int, int>> candidates;
    candidates.reserve(queried_pairs.size() + static_hits.size());
    auto add = [&](int a, int b) {
        const int ra = rank[static_cast<std::size_t>(a)];
        const int rb = rank[static_cast<std::size_t>(b)];
        candidates.emplace_back(std::min(ra, rb), std::max(ra, rb));
    };
    for (const auto& [a, b] : queried_pairs) {
        add(a, b);
    }
    for (const auto& [item, leaf] : static_hits) {
        add(item, static_index(leaf));
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

void run_physics_2d(ecs::World& world, JobSystem* jobs) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    wake_sleepers_by_velocity_2d(world);
    integrate_kinematic_2d(world, dt);
    ecs::update_world_transforms(world);

    // One gather per tick: colliders on movable bodies. Statics come from the cache in PhysicsBroadphase2D.
    std::vector<ecs::Entity> movable_entities;
    std::vector<ColliderItem2D> active = gather_active_colliders_2d(world, movable_entities);
    prepare_static_colliders_2d(world, std::move(movable_entities));

    // The platform has already moved. Wake before the sleeper integrates so it is not frozen this step.
    wake_sleepers_by_overlap_2d(world, active);
    integrate_dynamic_2d(world, dt);
    ecs::update_world_transforms(world);
    refresh_collider_poses_2d(world, active);

    // Events use this pose (pre-solve). The solver runs after publish and must not change their order.
    std::vector<ColliderItem2D> colliders;
    const std::vector<std::pair<int, int>> candidates = compute_broadphase_candidates_2d(world, active, colliders);

    publish_collision_events_2d(world, colliders, candidates);
    solve_physics_contacts_2d(world, colliders, candidates, jobs);
    ecs::update_world_transforms(world);
}

}
