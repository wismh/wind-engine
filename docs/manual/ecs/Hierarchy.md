# Hierarchy

An entity can hang from another entity. Its `Transform` is then **local** (relative to the parent), and the engine derives the world matrix from the chain. There is no scene graph: a child is an entity with a `Parent` component, plus free functions in `<engine/ecs/hierarchy.h>`.

---

## 1. Attaching an Entity

```cpp
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>

auto ship = world.create();
world.emplace<engine::Transform>(ship, engine::Transform{.position = {10.0f, 0.0f, 0.0f}});

auto turret = world.create();
world.emplace<engine::Transform>(turret, engine::Transform{.position = {0.0f, 1.0f, 0.0f}});

if (!engine::ecs::set_parent(world, turret, ship)) {
    // Refused: invalid entity, a cycle, or deeper than kMaxHierarchyDepth (32).
}
```

Always attach through `set_parent`. It keeps the derived `Children` list of the parent in sync. `set_parent` returns `false` and changes nothing for an invalid entity, `child == parent`, a cycle, or a chain deeper than `kMaxHierarchyDepth`.

By default the child's local `Transform` is left alone, so it jumps to wherever the new parent puts it. To keep the child where it is in the world, pass `KeepWorld::Yes`:

```cpp
engine::ecs::set_parent(world, coin, player, engine::ecs::KeepWorld::Yes); // coin does not move on screen
engine::ecs::clear_parent(world, coin, engine::ecs::KeepWorld::Yes);      // drop it again, still in place
```

`KeepWorld::Yes` recomputes the local position, rotation (a quaternion) and scale. A non-uniform scale above a rotated child can produce skew that `Transform` cannot store; the world position is kept exactly and rotation and scale are the best fit.

---

## 2. World Position and Matrix

```cpp
glm::vec3 where = engine::ecs::world_position(world, turret);   // {10, 1, 0}
glm::mat4 model = engine::ecs::world_matrix(world, turret);
engine::ecs::set_world_position(world, turret, {0.0f, 0.0f, 0.0f}); // writes the LOCAL position
```

`world_matrix` and `world_position` read the `WorldTransform` the engine updates in `Phase::Transform` (after `Game`, before `Render`). A `Game` system that edits a `Transform` and then reads `world_matrix` in the same system sees the old value. Call `engine::ecs::update_world_transforms(world)` first, or use `compute_world_matrix`, which walks the chain every time and is always exact.

Rotation is full 3D (`Transform::rotation` is a quaternion): a parent turned 90 degrees about Z (`engine::rotation_z(glm::half_pi<float>())`) swings a child offset `{2, 0, 0}` to world `{0, 2, 0}`.

---

## 3. What Follows the Parent

- Sprites, `Renderable` and `Local`-space particle emitters are drawn at the world matrix.
- `BoxCollider2D` and `CircleCollider2D` are tested at the world position (their size is not scaled by the parent).
- The active camera may be a child; the view is the inverse of its world matrix.
- A kinematic `RigidBody2D.velocity` moves the local position, so a kinematic child's velocity is in the parent's space. A dynamic child's velocity is world XY; the solver writes that body with `set_world_position`.

---

## 4. Destroying

Destroying a parent destroys all its descendants. Destroying a child removes it from its parent's `Children`. Inside a view the destroy is deferred like any other, and the cascade runs when the view ends.

---

## 5. Multiplayer Note

`Transform` and `Parent` are what the network layer replicates by default. `WorldTransform` and `Children` are derived and rebuilt on the receiving machine. `Entity` handles are not stable across machines, so a replicated entity has a `NetId` (`engine::net::NetIdentity`), and `Parent.entity` travels as the parent's `NetId` and is applied with `set_parent` on the client. The net layer does this itself; the tech note is `docs/tech/modules/Net.md`. With per-peer interest, a child sits in its root's grid cell, and a relevant child brings its replicated ancestors with it.

---

## Next Steps

- See where the world matrices are refreshed in [Systems & Schedules](Systems-and-Schedules.md).
- Attach a camera to a moving entity in [Camera](../rendering/Camera.md).
