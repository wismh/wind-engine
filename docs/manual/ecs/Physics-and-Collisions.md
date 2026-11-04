# Physics & Collisions

Wind provides an integrated collision probe for boxes and circles: velocity integration, overlap and trigger testing, and `CollisionEvent2D`s. It is a *probe*, not a physics solver. There is no contact resolution, no collision response, and no Z test: overlaps are tested on the XY plane only, and reacting to a hit is up to your systems.

The physics is 2D, and its names say so: `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `CollisionEvent2D`, and `run_physics_2d`, all in `<engine/ecs/physics_2d.h>`. `CollisionPhase` has no suffix because it does not depend on the dimension.

---

## 1. Colliders

Attach `engine::BoxCollider2D` (or `engine::CircleCollider2D`) and `engine::Transform` to entities. Colliders are centered on `Transform::position`. These types live in `engine`, not `engine::ecs`:

```cpp
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/transform.h>

// Spawn a solid wall
auto wall = world.create();
world.emplace<engine::Transform>(wall, engine::Transform{.position = {400.0f, 300.0f, 0.0f}});
world.emplace<engine::BoxCollider2D>(wall, engine::BoxCollider2D{
    .size = {64.0f, 64.0f, 1.0f},   // full extents, not half extents
    .layer = 1u << 0,               // what this collider is
    .mask = 1u << 1,                // what it detects (the player's layer)
    .is_trigger = false
});

// Spawn a coin trigger
auto coin = world.create();
world.emplace<engine::Transform>(coin, engine::Transform{.position = {200.0f, 150.0f, 0.0f}});
world.emplace<engine::CircleCollider2D>(coin, engine::CircleCollider2D{
    .radius = 16.0f,
    .layer = 1u << 2,
    .mask = 1u << 1,
    .is_trigger = true   // a trigger still reports Enter/Stay/Exit; the flag lets you tell it apart
});
```

Two colliders are tested against each other only when each one's `layer` hits the other's `mask`: `(a.layer & b.mask) != 0 && (b.layer & a.mask) != 0`. Give the player `layer = 1u << 1` and a `mask` that includes the wall and coin layers.

An entity with `engine::RigidBody2D` (a `velocity`) and a `Transform` is moved by `position += velocity * fixed_delta_time` in the same pass.
Colliders are tested at the entity's **world position**. A collider on a child entity (see [Hierarchy](Hierarchy.md)) follows its parent; its `size` and `radius` are world units and are not scaled or rotated by the parent. A `RigidBody2D` moves the local `Transform.position`, so the velocity of a child is in its parent's space.

---

## 2. Physics Runs by Itself

The engine registers `run_physics_2d` as a system on `Schedule::Fixed`, `Phase::Physics` for every world. You do not call it. Registering it again would integrate velocities twice. Put your own fixed logic on `Phase::Game`, which runs after it:

```cpp
void MyGame::on_start() {
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Game,
        [](engine::ecs::World& w) {
            // Runs after the engine's physics pass of the same step: read CollisionEvent2D here
        }
    );
}
```

`engine::run_physics_2d(world)` is public so a test can run one step by hand.

---

## 3. Handling `CollisionEvent2D`

For every overlapping pair each fixed step, `run_physics_2d` sends an `engine::CollisionEvent2D`. A pair that starts overlapping reports `Enter`, keeps reporting `Stay` while it overlaps, and reports `Exit` once when it stops:

```cpp
#include <engine/ecs/events.h>
#include <engine/ecs/physics_2d.h>

void collision_system(engine::ecs::World& world) {
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const engine::CollisionEvent2D& col : EventReader<engine::CollisionEvent2D>{
                 world, world.ctx<EventCursor<engine::CollisionEvent2D>>()}) {
        // col.a, col.b      the two entities (ordered, a < b)
        // col.phase         CollisionPhase::Enter, Stay or Exit
        // col.is_trigger    true if either collider is a trigger
        if (col.phase == engine::CollisionPhase::Enter) {
            handle_hit(world, col.a, col.b);
        }
    }
}
```

There is no contact normal or penetration depth. If you need a response (stopping a player at a wall), compute it from the two `Transform`s and colliders in your own system.

---

## 4. Grid Collision and Block Picking

For a world made of cells (voxel blocks, or a tile layer) use the grid queries in `<engine/ecs/grid_query.h>` instead of colliders. They are plain functions, not components or systems: you call them from your own movement system with a callable that says whether a cell is solid.

Grid space has cells of size 1: cell `(i, j, k)` covers `[i, i+1)` on each axis, and +Y is up. If your cells are not 1 world unit, divide positions and deltas by the cell size before the call and multiply the result back.

### Moving a box: `sweep_aabb`

`sweep_aabb(box, delta, is_solid)` moves a `render::Aabb` one axis at a time (Y, then X, then Z) and stops each axis flush against the first solid face. It returns the resolved box, the `moved` displacement, and a `hit` per axis (`-1` blocked moving down that axis, `+1` moving up, `0` free). `grounded()` is `hit.y < 0`.

```cpp
#include <engine/ecs/grid_query.h>

// Schedule::Fixed, Phase::Game. `blocks` is your chunk storage.
const float dt = world.ctx<engine::Time>().fixed_delta_time;
player.velocity.y -= 30.0f * dt;
const engine::render::Aabb box{player.feet - glm::vec3{0.3f, 0.0f, 0.3f}, player.feet + glm::vec3{0.3f, 1.8f, 0.3f}};
const engine::GridSweep result = engine::sweep_aabb(box, player.velocity * dt,
        [&](glm::ivec3 cell) { return blocks.is_solid(cell); });

player.feet = {(result.box.min.x + result.box.max.x) * 0.5f, result.box.min.y, (result.box.min.z + result.box.max.z) * 0.5f};
if (result.hit.y != 0) {
    player.velocity.y = 0.0f;   // landed or bumped the ceiling
}
player.on_ground = result.grounded();
```

- A box standing on a floor under gravity snaps to exactly the same height every step and reports `grounded()`: no jitter, no slow sink.
- A box flush against a wall slides along it; only the blocked axis stops.
- No speed tunnels through a wall: every face along the way is tested, and a long move is cut into sub-steps of at most one cell (up to 64).
- There is no step-up: a one-block ledge stops horizontal motion. Jump over it, or add step logic in your game.
- A box that already overlaps a solid cell (spawned inside a block) is not pushed out, but it is never stuck either: it moves freely out of it.

### Picking a block: `raycast_grid`

`raycast_grid(ray, max_distance, is_solid)` walks the cells along a `Ray` and returns the first solid one: its `cell`, the `normal` of the face the ray entered, `t` and `point`. `cell + normal` is the empty cell in front of that face, which is where a placed block goes.

```cpp
const engine::Ray ray{eye_position, look_direction};   // unit direction: t and max_distance are distances
if (const auto hit = engine::raycast_grid(ray, 5.0f, [&](glm::ivec3 c) { return blocks.is_solid(c); })) {
    if (break_pressed) {
        blocks.set(hit->cell, Block::Air);
    } else if (place_pressed && hit->normal != glm::ivec3{0}) {
        blocks.set(hit->cell + hit->normal, Block::Stone);
    }
}
```

A zero `normal` means the ray started inside a solid cell. A ray that passes exactly through an edge or corner between cells is stopped by any solid cell touching it there.

The callable is only borrowed for the call: pass a lambda directly, and do not keep the `CellPredicate` around.

### 2D and tilemaps

Both functions have 2D overloads over `glm::vec2` and `glm::ivec2`: `raycast_grid(origin, direction, max_distance, is_solid)` and `sweep_aabb(min, max, delta, is_solid)`, returning `GridHit2D` and `GridSweep2D`.

A tile layer has no collision flag of its own; you decide which tiles are solid. Tilemap rows run down while y runs up, so grid cell `(x, y)` is tile row `-y - 1`:

```cpp
const glm::vec2 cell = map_asset.cell_size();
const glm::vec2 origin{map_transform.position};   // map's top-left corner, unrotated and unscaled
const auto solid = [&](glm::ivec2 c) { return map.tile(/*layer_index=*/0, c.x, -c.y - 1) != 0; };

const engine::GridSweep2D r = engine::sweep_aabb((box_min - origin) / cell, (box_max - origin) / cell, delta / cell, solid);
box_min = origin + r.min * cell;
box_max = origin + r.max * cell;
```

---

## Next Steps

- Make tile layers solid with [Tilemaps](../rendering/Tilemaps.md).
- Render your entities with sprites in [Sprites & Animation](../rendering/Sprites-and-Animation.md).
- Control the viewport using the [Camera](../rendering/Camera.md).
