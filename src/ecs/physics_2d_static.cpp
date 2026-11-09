#include "physics_2d_internal.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/log.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace engine {
namespace {

// Cached statics re-read per tick by the audit. Constant, so the per-tick cost does not follow the static count.
inline constexpr std::size_t kStaticAuditPerTick = 32;

[[nodiscard]] bool same_bits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

[[nodiscard]] bool same_bits(glm::vec2 a, glm::vec2 b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y);
}

[[nodiscard]] bool same_bits(glm::vec3 a, glm::vec3 b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z);
}

// Bitwise, so a NaN field that has not changed still compares equal.
[[nodiscard]] bool same_item(const ColliderItem2D& a, const ColliderItem2D& b) {
    if (a.entity != b.entity || a.shape != b.shape || a.part != b.part || a.layer != b.layer || a.mask != b.mask ||
        a.is_trigger != b.is_trigger || a.movable != b.movable || a.polygon_count != b.polygon_count) {
        return false;
    }
    if (!same_bits(a.position, b.position) || !same_bits(a.angle, b.angle) || !same_bits(a.friction, b.friction) ||
        !same_bits(a.restitution, b.restitution) || !same_bits(a.box_size, b.box_size) ||
        !same_bits(a.radius, b.radius)) {
        return false;
    }
    for (int i = 0; i < a.polygon_count; ++i) {
        if (!same_bits(a.polygon[i], b.polygon[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool contains_sorted(const std::vector<ecs::Entity>& sorted, ecs::Entity entity) {
    return std::binary_search(sorted.begin(), sorted.end(), entity);
}

// An entity that turned movable while cached as static, or that stopped being movable and still has a collider.
[[nodiscard]] bool active_set_moved_statics(const ecs::World& world, const PhysicsBroadphase2D& cache,
                                            const std::vector<ecs::Entity>& now) {
    std::vector<ecs::Entity> entered;
    std::set_difference(now.begin(), now.end(), cache.active_entities.begin(), cache.active_entities.end(),
                        std::back_inserter(entered));
    for (ecs::Entity entity : entered) {
        if (contains_sorted(cache.static_entities, entity)) {
            return true;
        }
    }
    std::vector<ecs::Entity> left;
    std::set_difference(cache.active_entities.begin(), cache.active_entities.end(), now.begin(), now.end(),
                        std::back_inserter(left));
    for (ecs::Entity entity : left) {
        if (world.valid(entity) && has_collider_2d(world, entity)) {
            return true;
        }
    }
    return false;
}

void warn_once(PhysicsBroadphase2D& cache, ecs::Entity entity) {
    const auto it = std::lower_bound(cache.audit_warned.begin(), cache.audit_warned.end(), entity);
    if (it != cache.audit_warned.end() && *it == entity) {
        return;
    }
    cache.audit_warned.insert(it, entity);
    log::warn("physics 2D: the static collider on entity " + std::to_string(entity.index) +
              " changed in place without mark_static_colliders_dirty_2d; the static cache was rebuilt late. Call "
              "it after editing static geometry, or give geometry that moves every tick a Kinematic body.");
}

[[nodiscard]] bool key_less(ecs::Entity entity_a, ColliderShape2D shape_a, std::uint32_t part_a, ecs::Entity entity_b,
                            ColliderShape2D shape_b, std::uint32_t part_b) {
    if (entity_a != entity_b) {
        return entity_a < entity_b;
    }
    if (shape_a != shape_b) {
        return static_cast<int>(shape_a) < static_cast<int>(shape_b);
    }
    return part_a < part_b;
}

// Re-reads the one collider behind `cached` and compares it. A compound part also compares the shape count it was
// read with, so a compound that grew or shrank in place is found as well.
[[nodiscard]] bool static_item_matches(const ecs::World& world, const ColliderItem2D& cached,
                                       std::uint32_t compound_size) {
    if (!world.valid(cached.entity)) {
        return false;
    }
    const Transform* transform = world.try_get<Transform>(cached.entity);
    if (transform == nullptr) {
        return false;
    }
    const glm::mat4 matrix = collider_world_matrix_2d(world, cached.entity, *transform);
    switch (cached.shape) {
    case ColliderShape2D::Box: {
        const BoxCollider2D* box = world.try_get<BoxCollider2D>(cached.entity);
        return box != nullptr && same_item(make_box_item_2d(cached.entity, matrix, *box, false), cached);
    }
    case ColliderShape2D::Circle: {
        const CircleCollider2D* circle = world.try_get<CircleCollider2D>(cached.entity);
        return circle != nullptr && same_item(make_circle_item_2d(cached.entity, matrix, *circle, false), cached);
    }
    case ColliderShape2D::Polygon: {
        const PolygonCollider2D* polygon = world.try_get<PolygonCollider2D>(cached.entity);
        return polygon != nullptr && same_item(make_polygon_item_2d(cached.entity, matrix, *polygon, false), cached);
    }
    case ColliderShape2D::Compound: {
        const CompoundCollider2D* compound = world.try_get<CompoundCollider2D>(cached.entity);
        return compound != nullptr && compound->shapes.size() == compound_size && cached.part < compound_size &&
               same_item(make_compound_item_2d(cached.entity, matrix, *compound, cached.part, false), cached);
    }
    }
    return false;
}

// Re-reads a constant number of cached statics, round robin. A mismatch dirties the cache.
void audit_static_cache(const ecs::World& world, PhysicsBroadphase2D& cache) {
    const std::size_t total = cache.static_slots.size() + cache.static_triggers.size();
    if (total == 0) {
        return;
    }
    const std::size_t count = std::min(kStaticAuditPerTick, total);
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t index = cache.audit_cursor % total;
        cache.audit_cursor = (index + 1) % total;
        const ColliderItem2D* cached = nullptr;
        std::uint32_t compound_size = 0;
        if (index < cache.static_slots.size()) {
            const PhysicsBroadphase2D::Slot& slot = cache.static_slots[index];
            if (!slot.live) {
                continue;
            }
            cached = &slot.item;
            compound_size = slot.compound_size;
        } else {
            cached = &cache.static_triggers[index - cache.static_slots.size()];
        }
        if (!static_item_matches(world, *cached, compound_size)) {
            cache.dirty = true;
            warn_once(cache, cached->entity);
            return;
        }
    }
}

[[nodiscard]] int allocate_slot(PhysicsBroadphase2D& cache) {
    if (!cache.free_slots.empty()) {
        const int slot = cache.free_slots.back();
        cache.free_slots.pop_back();
        return slot;
    }
    cache.static_slots.emplace_back();
    return static_cast<int>(cache.static_slots.size()) - 1;
}

[[nodiscard]] bool item_key_less(const ColliderItem2D& a, const ColliderItem2D& b) {
    return key_less(a.entity, a.shape, a.part, b.entity, b.shape, b.part);
}

// Merges `old_keys` with the solids among `items`, both sorted by (entity, shape, part), into `out`: a key with no item
// drops its slot and tree leaf, an item with no key gets a new slot and leaf, and a kept key whose item changed has its
// slot and leaf updated. Triggers in `items` are skipped.
void merge_static_slots(const ecs::World& world, PhysicsBroadphase2D& cache,
                        std::span<const PhysicsBroadphase2D::Key> old_keys, std::span<const ColliderItem2D> items,
                        std::vector<PhysicsBroadphase2D::Key>& out) {
    std::size_t old_index = 0;
    std::size_t new_index = 0;
    auto skip_triggers = [&] {
        while (new_index < items.size() && items[new_index].is_trigger) {
            ++new_index;
        }
    };
    auto drop = [&](int slot) {
        cache.static_tree.erase(slot);
        cache.static_slots[static_cast<std::size_t>(slot)] = PhysicsBroadphase2D::Slot{};
        cache.free_slots.push_back(slot);
    };
    auto compound_size_of = [&](const ColliderItem2D& item) -> std::uint32_t {
        if (item.shape != ColliderShape2D::Compound) {
            return 0;
        }
        return static_cast<std::uint32_t>(world.get<CompoundCollider2D>(item.entity).shapes.size());
    };
    skip_triggers();
    while (old_index < old_keys.size() || new_index < items.size()) {
        const bool has_old = old_index < old_keys.size();
        const bool has_new = new_index < items.size();
        const PhysicsBroadphase2D::Key* old_key = has_old ? &old_keys[old_index] : nullptr;
        const ColliderItem2D* item = has_new ? &items[new_index] : nullptr;
        if (has_old && (!has_new || key_less(old_key->entity, old_key->shape, old_key->part, item->entity,
                                             item->shape, item->part))) {
            drop(old_key->slot);
            ++old_index;
            continue;
        }
        if (has_old && old_key->entity == item->entity && old_key->shape == item->shape &&
            old_key->part == item->part) {
            PhysicsBroadphase2D::Slot& slot = cache.static_slots[static_cast<std::size_t>(old_key->slot)];
            slot.compound_size = compound_size_of(*item);
            if (!same_item(slot.item, *item)) {
                slot.item = *item;
                cache.static_tree.set(old_key->slot, aabb_of_2d(*item));
            }
            out.push_back(*old_key);
            ++old_index;
        } else {
            const int slot = allocate_slot(cache);
            cache.static_slots[static_cast<std::size_t>(slot)] =
                    PhysicsBroadphase2D::Slot{.item = *item, .compound_size = compound_size_of(*item), .live = true};
            cache.static_tree.set(slot, aabb_of_2d(*item));
            out.push_back(PhysicsBroadphase2D::Key{
                    .entity = item->entity, .shape = item->shape, .part = item->part, .slot = slot});
        }
        ++new_index;
        skip_triggers();
    }
}

// Re-reads every static collider, then edits the static tree only where a collider was added, removed or changed.
void rebuild_static_cache(ecs::World& world, PhysicsBroadphase2D& cache) {
    std::vector<ColliderItem2D> items;
    auto is_active = [&](ecs::Entity entity) { return contains_sorted(cache.active_entities, entity); };
    world.view<Transform, BoxCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const BoxCollider2D& box) {
                if (!is_active(entity)) {
                    items.push_back(make_box_item_2d(entity, collider_world_matrix_2d(world, entity, transform), box,
                                                     false));
                }
            });
    world.view<Transform, CircleCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const CircleCollider2D& circle) {
                if (!is_active(entity)) {
                    items.push_back(make_circle_item_2d(entity, collider_world_matrix_2d(world, entity, transform),
                                                        circle, false));
                }
            });
    world.view<Transform, PolygonCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const PolygonCollider2D& polygon) {
                if (!is_active(entity)) {
                    items.push_back(make_polygon_item_2d(entity, collider_world_matrix_2d(world, entity, transform),
                                                         polygon, false));
                }
            });
    world.view<Transform, CompoundCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const CompoundCollider2D& compound) {
                if (is_active(entity)) {
                    return;
                }
                const glm::mat4 matrix = collider_world_matrix_2d(world, entity, transform);
                const auto count = static_cast<std::uint32_t>(compound.shapes.size());
                for (std::uint32_t part = 0; part < count; ++part) {
                    items.push_back(make_compound_item_2d(entity, matrix, compound, part, false));
                }
            });
    std::sort(items.begin(), items.end(), item_key_less);

    cache.static_entities.clear();
    cache.static_triggers.clear();
    for (const ColliderItem2D& item : items) {
        if (cache.static_entities.empty() || cache.static_entities.back() != item.entity) {
            cache.static_entities.push_back(item.entity);
        }
        if (item.is_trigger) {
            cache.static_triggers.push_back(item);
        }
    }
    std::sort(cache.static_triggers.begin(), cache.static_triggers.end(), collider_order_less_2d);

    std::vector<PhysicsBroadphase2D::Key> keys;
    keys.reserve(items.size());
    merge_static_slots(world, cache, cache.static_keys, items, keys);
    cache.static_keys = std::move(keys);
    cache.box_version = world.structure_version<BoxCollider2D>();
    cache.circle_version = world.structure_version<CircleCollider2D>();
    cache.polygon_version = world.structure_version<PolygonCollider2D>();
    cache.compound_version = world.structure_version<CompoundCollider2D>();
    cache.marked_entities.clear();
    cache.audit_cursor = 0;
    cache.dirty = false;
    cache.last_rebuild_reads = items.size();
    ++cache.rebuild_count;
}

// The marked entities and their descendants that are not movable, sorted and unique. The whole subtree is walked: an
// edited ancestor Transform moves every descendant, and the movable set filters out the ones that are not static.
[[nodiscard]] std::vector<ecs::Entity> marked_static_subtrees(const ecs::World& world,
                                                              const PhysicsBroadphase2D& cache) {
    std::vector<ecs::Entity> entities;
    std::vector<ecs::Entity> stack;
    for (ecs::Entity marked : cache.marked_entities) {
        if (world.valid(marked)) {
            stack.push_back(marked);
        }
    }
    while (!stack.empty()) {
        const ecs::Entity entity = stack.back();
        stack.pop_back();
        if (!contains_sorted(cache.active_entities, entity)) {
            entities.push_back(entity);
        }
        const Children* children = world.try_get<Children>(entity);
        if (children == nullptr) {
            continue;
        }
        for (ecs::Entity child : children->entities) {
            const Parent* parent = world.try_get<Parent>(child);
            if (world.valid(child) && parent != nullptr && parent->entity == entity) {
                stack.push_back(child);
            }
        }
    }
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    return entities;
}

// Re-reads only the static colliders of the marked subtrees and replaces only their keys, slots and tree leaves. The
// cache ends as a full rebuild would leave it: keys, static entities and triggers keep their canonical order.
void rebuild_marked_statics(ecs::World& world, PhysicsBroadphase2D& cache) {
    const std::vector<ecs::Entity> entities = marked_static_subtrees(world, cache);
    cache.marked_entities.clear();

    std::vector<ColliderItem2D> items;
    for (ecs::Entity entity : entities) {
        append_colliders_2d(world, entity, false, true, items);
    }
    std::sort(items.begin(), items.end(), item_key_less);

    // Splice: keys of other entities are copied, each re-read entity's range is merged with its new items.
    const std::vector<PhysicsBroadphase2D::Key>& old_keys = cache.static_keys;
    std::vector<PhysicsBroadphase2D::Key> keys;
    keys.reserve(old_keys.size() + items.size());
    auto key_before = [](const PhysicsBroadphase2D::Key& key, ecs::Entity entity) { return key.entity < entity; };
    auto key_after = [](ecs::Entity entity, const PhysicsBroadphase2D::Key& key) { return entity < key.entity; };
    auto old_cursor = old_keys.begin();
    std::size_t item_cursor = 0;
    std::vector<ecs::Entity> read_entities;  // re-read entities that still have a collider, sorted
    for (ecs::Entity entity : entities) {
        const auto first = std::lower_bound(old_cursor, old_keys.end(), entity, key_before);
        const auto last = std::upper_bound(first, old_keys.end(), entity, key_after);
        keys.insert(keys.end(), old_cursor, first);
        std::size_t item_end = item_cursor;
        while (item_end < items.size() && items[item_end].entity == entity) {
            ++item_end;
        }
        if (item_end > item_cursor) {
            read_entities.push_back(entity);
        }
        merge_static_slots(world, cache, std::span(first, last),
                           std::span(items).subspan(item_cursor, item_end - item_cursor), keys);
        old_cursor = last;
        item_cursor = item_end;
    }
    keys.insert(keys.end(), old_cursor, old_keys.end());
    cache.static_keys = std::move(keys);

    std::vector<ecs::Entity> kept;
    kept.reserve(cache.static_entities.size() + read_entities.size());
    std::set_difference(cache.static_entities.begin(), cache.static_entities.end(), entities.begin(), entities.end(),
                        std::back_inserter(kept));
    cache.static_entities.clear();
    std::merge(kept.begin(), kept.end(), read_entities.begin(), read_entities.end(),
               std::back_inserter(cache.static_entities));

    std::erase_if(cache.static_triggers,
                  [&](const ColliderItem2D& trigger) { return contains_sorted(entities, trigger.entity); });
    bool new_trigger = false;
    for (const ColliderItem2D& item : items) {
        if (item.is_trigger) {
            cache.static_triggers.push_back(item);
            new_trigger = true;
        }
    }
    if (new_trigger) {
        std::sort(cache.static_triggers.begin(), cache.static_triggers.end(), collider_order_less_2d);
    }
    cache.last_rebuild_reads = items.size();
    ++cache.entity_rebuild_count;
}

}

void prepare_static_colliders_2d(ecs::World& world, std::vector<ecs::Entity> movable_entities) {
    PhysicsBroadphase2D& cache = world.ctx<PhysicsBroadphase2D>();

    // A collider added or removed anywhere, an entity destroyed or recycled.
    if (cache.box_version != world.structure_version<BoxCollider2D>() ||
        cache.circle_version != world.structure_version<CircleCollider2D>() ||
        cache.polygon_version != world.structure_version<PolygonCollider2D>() ||
        cache.compound_version != world.structure_version<CompoundCollider2D>()) {
        cache.dirty = true;
    }

    // A body added, removed, retyped, or a collider reparented in or out of a movable body.
    std::vector<ecs::Entity>& now = movable_entities;
    if (now != cache.active_entities) {
        if (active_set_moved_statics(world, cache, now)) {
            cache.dirty = true;
        }
        cache.active_entities = std::move(now);
    }

    // A world-wide mark (or any of the above) wins over the per-entity marks: the full re-read covers them.
    if (!cache.dirty && !cache.marked_entities.empty()) {
        rebuild_marked_statics(world, cache);
    }
    if (!cache.dirty) {
        audit_static_cache(world, cache);
    }
    if (cache.dirty) {
        rebuild_static_cache(world, cache);
    }
}

void mark_static_colliders_dirty_2d(ecs::World& world) {
    world.ctx<PhysicsBroadphase2D>().dirty = true;
}

void mark_static_colliders_dirty_2d(ecs::World& world, ecs::Entity entity) {
    if (world.valid(entity)) {
        world.ctx<PhysicsBroadphase2D>().marked_entities.push_back(entity);
    }
}

}
