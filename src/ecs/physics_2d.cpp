#include <engine/ecs/physics_2d.h>

#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>

#include "broadphase.h"
#include "physics_2d_internal.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace engine {

std::vector<std::pair<int, int>> compute_broadphase_candidates_2d(
        ecs::World& world, const std::vector<ColliderItem2D>& colliders) {
    if (colliders.empty()) {
        return {};
    }

    struct DynamicItem {
        int collider_index;
        Aabb box;
    };

    std::vector<StaticColliderItem2D> static_items;
    std::vector<DynamicItem> dynamic_items;
    static_items.reserve(colliders.size());

    for (std::size_t i = 0; i < colliders.size(); ++i) {
        const ColliderItem2D& item = colliders[i];
        const bool is_movable = has_movable_body_2d(world, item.entity);
        const bool is_active = is_movable || item.is_trigger;
        const Aabb box = aabb_of_2d(item);

        if (is_active) {
            dynamic_items.push_back(DynamicItem{.collider_index = static_cast<int>(i), .box = box});
        } else {
            static_items.push_back(StaticColliderItem2D{
                    .entity = item.entity,
                    .box = box,
                    .collider_index = static_cast<int>(i),
            });
        }
    }

    // Static-static pairs are never queried. If there are no dynamic, kinematic or trigger colliders,
    // there are no candidate pairs to produce.
    if (dynamic_items.empty()) {
        return {};
    }

    PhysicsBroadphase2D& bp = world.ctx<PhysicsBroadphase2D>();

    // Static tree remains valid across ticks until static scene geometry changes.
    bool static_changed = bp.static_dirty || (static_items.size() != bp.static_items.size());
    if (!static_changed) {
        for (std::size_t s = 0; s < static_items.size(); ++s) {
            if (static_items[s].entity != bp.static_items[s].entity ||
                static_items[s].box.min != bp.static_items[s].box.min ||
                static_items[s].box.max != bp.static_items[s].box.max) {
                static_changed = true;
                break;
            }
        }
    }

    if (static_changed) {
        bp.static_items = std::move(static_items);
        std::vector<Aabb> static_boxes;
        static_boxes.reserve(bp.static_items.size());
        for (const auto& s_item : bp.static_items) {
            static_boxes.push_back(s_item.box);
        }
        bp.static_tree.rebuild(static_boxes);
        bp.static_dirty = false;
    } else {
        // Reuse static tree; update collider index mappings for the current frame.
        for (std::size_t s = 0; s < static_items.size(); ++s) {
            bp.static_items[s].collider_index = static_items[s].collider_index;
        }
    }

    std::vector<std::pair<int, int>> candidates;

    // 1. Query Dynamic vs Dynamic
    std::vector<Aabb> dynamic_boxes;
    dynamic_boxes.reserve(dynamic_items.size());
    for (const auto& d_item : dynamic_items) {
        dynamic_boxes.push_back(d_item.box);
    }
    bp.dynamic_tree.rebuild(dynamic_boxes);

    std::vector<std::pair<int, int>> dyn_pairs;
    bp.dynamic_tree.query_pairs(dyn_pairs);
    for (const auto& [d1, d2] : dyn_pairs) {
        const int idx_a = dynamic_items[static_cast<std::size_t>(d1)].collider_index;
        const int idx_b = dynamic_items[static_cast<std::size_t>(d2)].collider_index;
        candidates.emplace_back(std::min(idx_a, idx_b), std::max(idx_a, idx_b));
    }

    // 2. Query Dynamic vs Static (Never Static vs Static)
    if (!bp.static_items.empty()) {
        for (const auto& d_item : dynamic_items) {
            bp.static_tree.query(d_item.box, [&](int s) {
                const int static_idx = bp.static_items[static_cast<std::size_t>(s)].collider_index;
                candidates.emplace_back(std::min(d_item.collider_index, static_idx),
                                        std::max(d_item.collider_index, static_idx));
            });
        }
    }

    std::sort(candidates.begin(), candidates.end());
    const auto last = std::unique(candidates.begin(), candidates.end());
    candidates.erase(last, candidates.end());
    return candidates;
}

void run_physics_2d(ecs::World& world, JobSystem* jobs) {
    const float dt = world.ctx<Time>().fixed_delta_time;

    wake_sleepers_by_velocity_2d(world);
    integrate_kinematic_2d(world, dt);
    ecs::update_world_transforms(world);

    // The platform has already moved. Wake before the sleeper integrates so it is not frozen this step.
    wake_sleepers_by_overlap_2d(world);
    integrate_dynamic_2d(world, dt);
    ecs::update_world_transforms(world);

    // Events use this pose (pre-solve). The solver runs after publish and must not change their order.
    const std::vector<ColliderItem2D> colliders = gather_colliders_2d(world);
    if (colliders.empty()) {
        publish_collision_events_2d(world, colliders, {});
        solve_physics_contacts_2d(world, colliders, {}, jobs);
        ecs::update_world_transforms(world);
        return;
    }

    const std::vector<std::pair<int, int>> candidates = compute_broadphase_candidates_2d(world, colliders);

    publish_collision_events_2d(world, colliders, candidates);
    solve_physics_contacts_2d(world, colliders, candidates, jobs);
    ecs::update_world_transforms(world);
}

}
