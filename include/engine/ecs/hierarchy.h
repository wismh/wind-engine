#pragma once

// docs/tech/modules/ECS.md (Hierarchy)

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace engine {

// Plain data, replicated with `Transform`. Set it through `ecs::set_parent`, not by emplacing it by hand.
struct Parent {
    ecs::Entity entity{};
};

// Derived index of the entities whose `Parent` is this entity. Managed only by `ecs::set_parent`,
// `ecs::clear_parent` and entity destroy. Never replicated, never edited by hand.
struct Children {
    std::vector<ecs::Entity> entities;
};

// Derived world matrix of an entity that has a `Parent`. Written only by `ecs::update_world_transforms`.
// Never replicated.
struct WorldTransform {
    glm::mat4 matrix{1.0f};
};

}

namespace engine::ecs {

// A root has depth 0. `set_parent` rejects a change that would put any entity deeper than this.
inline constexpr int kMaxHierarchyDepth = 32;

enum class KeepWorld {
    No,  // the child's local Transform is untouched; it jumps to wherever the new parent puts it
    Yes, // the child's local Transform is recomputed so its world matrix does not change
};

// Makes `parent` the parent of `child`. Returns false and changes nothing when either entity is invalid, when
// child == parent, when `parent` is a descendant of `child` (cycle), or when the result would be deeper than
// kMaxHierarchyDepth (counting the child's own subtree). Setting the current parent again succeeds and does nothing.
// An entity without Transform is a valid parent or child; it counts as identity. With KeepWorld::Yes a child without
// Transform gets one holding the recomputed local transform.
// KeepWorld::Yes decomposes the new local matrix into position, X-then-Y-then-Z Euler radians and scale. If the
// parent chain has non-uniform scale above a rotation, the true local matrix has skew that Transform cannot store:
// the child's world position is kept exactly, rotation and scale are the best fit.
[[nodiscard]] bool set_parent(World& world, Entity child, Entity parent, KeepWorld keep = KeepWorld::No);

// Removes the child's Parent (and its WorldTransform) and takes it out of its parent's Children. No-op without a
// Parent. KeepWorld::Yes writes the former world matrix into the child's local Transform.
void clear_parent(World& world, Entity child, KeepWorld keep = KeepWorld::No);

// WorldTransform if the entity has one, else model_matrix(Transform), else identity. Only as fresh as the last
// `update_world_transforms`; it ignores the parent chain when WorldTransform is missing.
[[nodiscard]] glm::mat4 world_matrix(const World& world, Entity entity);

// Exact world matrix: walks up the Parent chain every call and ignores WorldTransform. Slow path for code that
// cannot wait for the propagation phase. A dead Parent ends the walk (the entity is treated as a root).
[[nodiscard]] glm::mat4 compute_world_matrix(const World& world, Entity entity);

[[nodiscard]] glm::vec3 world_position(const World& world, Entity entity);

// Moves the entity so its world position becomes `position` by writing its LOCAL Transform (through the exact parent
// chain). No-op when the entity is invalid or has no Transform.
void set_world_position(World& world, Entity entity, glm::vec3 position);

// Recomputes WorldTransform for every entity that has a valid Parent, parents before children. Idempotent. Removes
// WorldTransform from entities that no longer have a Parent. A Parent whose entity is dead is logged once and cleared,
// which makes the entity a root.
void update_world_transforms(World& world);

}
