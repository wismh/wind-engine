# Physics & Collisions

Wind provides 2D box and circle collisions: velocity integration, overlap and trigger events, and a positional solver for dynamic bodies. `CollisionEvent2D` carries a contact normal, a penetration, and one world-space point from the pose *before* the solver runs. A kinematic body (the default) still integrates and tunnels through overlaps. A dynamic body can land on a static collider, slide along a wall, and be pushed by a moving kinematic platform. There is no Z test, no friction, and no bounce. Overlaps are tested on the XY plane only.

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

`engine::BodyType2D` on `engine::RigidBody2D` chooses how a body moves. The default is `Kinematic`.

- `Static` does not integrate and is not moved by contacts. A collider with no `RigidBody2D` is static too.
- `Kinematic` does `local position += velocity * fixed_delta_time` and is not moved by contacts. Two kinematics still tunnel. The solver does not change a kinematic velocity.
- `Dynamic` adds gravity, then moves in world space. `velocity.xy` is world XY and `velocity.z` is world z. Inverse mass is `1 / max(mass, kMinDynamicMass)` (a non-positive or non-finite mass uses the minimum). The positional solver separates it from non-trigger colliders.

Gravity lives on `world.ctx<engine::Physics2DConfig>()`. That resource is created on first use with gravity zero, so an old scene does not fall until you set it. It is not a serialized component. Only dynamic bodies use it, scaled by `gravity_scale` (default 1).

```cpp
world.ctx<engine::Physics2DConfig>().gravity = {0.f, -30.f, 0.f};

auto floor = world.create();
world.emplace<engine::Transform>(floor, engine::Transform{.position = {0.f, -1.f, 0.f}});
world.emplace<engine::BoxCollider2D>(floor, engine::BoxCollider2D{.size = {8.f, 2.f, 1.f}});

auto crate = world.create();
world.emplace<engine::Transform>(crate, engine::Transform{.position = {0.f, 2.f, 0.f}});
world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{
    .type = engine::BodyType2D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::BoxCollider2D>(crate, engine::BoxCollider2D{.size = {2.f, 2.f, 1.f}});
```

The crate falls, then rests with its bottom within `kContactSlop2D` (`1e-3`) of the floor. The solver runs 8 iterations (`kPositionIterations2D`) and does not bounce. A trigger still reports Enter, Stay, and Exit and never pushes. A layer mismatch is not solved. Resting overlap stays inside the slop and keeps reporting Stay. Once the shapes no longer overlap, the next step reports Exit. The event's penetration is the overlap before that correction, so a resting contact publishes the sunk pose from integration, not the separated pose.

A kinematic platform pushes a dynamic body along the contact normal and is not slowed. Its world velocity is the parent's world linear map (rotation and scale) times its local velocity. A root's `velocity.xy` is already world XY; the body's own rotation is not applied. There is no friction, so a floor that slides sideways under a crate does not drag it.

Do not put `BodyType2D::Dynamic` on a predicted or controlled entity. Prediction replays `step`, and `run_physics_2d` is a second simulation of that entity.

Colliders are tested at the entity's **world position**. A collider on a child entity (see [Hierarchy](Hierarchy.md)) follows its parent; its `size` and `radius` are world units and are not scaled or rotated by the parent. The contact uses that same axis-aligned XY shape. A kinematic child's velocity is in its parent's space. A dynamic child is allowed: the solver writes the corrected world position through `set_world_position`, so a translated or rotated parent keeps the contact in world space. The dynamic velocity itself stays world XY and does not include the parent's carry.

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

`a` and `b` are ordered so `a < b`. `normal` is a unit XY vector from `a` toward `b`. It is the thinner overlap axis for two boxes (X wins a tie; a zero center-delta on the chosen axis points along the positive axis, so coincident centers use +X), the direction from center to center for two circles, and the closest-point direction for a box and a circle (the nearest face when the circle center is inside the box). `penetration` is the overlap along that normal and is `>= 0`. A penetration of 0 is a touch and still an event. `point` is one world-space point between the shapes, the deepest sample. `Exit` sets `normal`, `penetration`, and `point` to zero and keeps the `is_trigger` value from the last overlapping step. A trigger still reports Enter, Stay, and Exit, still sets `is_trigger`, and still carries a normal, a penetration, and a point. Events are published before the solver, in the same gather order as before. The solver does not change a kinematic velocity, so a kinematic character still cancels its own approach here.

```cpp
#include <engine/ecs/events.h>
#include <engine/ecs/physics_2d.h>

// `player` is a root kinematic character. The solver does not change this velocity.
void collision_system(engine::ecs::World& world, engine::ecs::Entity player) {
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const engine::CollisionEvent2D& col : EventReader<engine::CollisionEvent2D>{
                 world, world.ctx<EventCursor<engine::CollisionEvent2D>>()}) {
        // col.a, col.b         the two entities (a < b)
        // col.phase            Enter, Stay, or Exit
        // col.is_trigger       true if either collider is a trigger
        // col.normal           unit XY from a toward b; (0, 0) on Exit
        // col.penetration      overlap along normal, >= 0; 0 on Exit
        // col.point            one world-space point; (0, 0) on Exit
        if (col.phase == engine::CollisionPhase::Exit || col.is_trigger) {
            continue;  // a trigger still has a contact; skip it when you want a wall
        }
        if (col.a != player && col.b != player) {
            continue;
        }
        engine::RigidBody2D* body = world.try_get<engine::RigidBody2D>(player);
        if (body == nullptr) {
            continue;
        }
        // The next fixed step integrates whatever velocity you leave. This sample only cancels
        // approach. normal is world XY, and a root's local axes match the world. A child's
        // RigidBody2D.velocity is in its parent's space: rotate the normal into that space first.
        const glm::vec2 n = col.b == player ? col.normal : -col.normal;
        const float into = body->velocity.x * n.x + body->velocity.y * n.y;
        if (into < 0.f) {
            body->velocity.x -= into * n.x;
            body->velocity.y -= into * n.y;
        }
    }
}
```

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
