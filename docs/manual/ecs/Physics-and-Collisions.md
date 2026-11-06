# Physics & Collisions

Wind has two solvers. The 2D solver covers boxes, circles, and convex polygons: velocity integration, yaw about world Z, overlap and trigger events, and a positional solver for dynamic bodies. `CollisionEvent2D` carries a contact normal, a penetration, and one world-space point from the pose *before* the solver runs. A kinematic body (the default) still integrates and tunnels through overlaps. A dynamic body can land on a static collider, slide along a wall, tumble when friction has a lever arm, and be pushed by a moving kinematic platform. There is no Z test. Friction and restitution default to 0. Overlaps are tested on the XY plane only.

The 3D solver is separate. It does not lock Z on the 2D shapes, and it does not read 2D colliders. Spheres, boxes, and capsules live in `<engine/ecs/physics_3d.h>` and report `CollisionEvent3D`. Characters and tiles stay on `sweep_aabb`. Do not mark a predicted entity `Dynamic` in either solver.

The 2D names say so: `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `PolygonCollider2D`, `CollisionEvent2D`, and `run_physics_2d`, all in `<engine/ecs/physics_2d.h>`. `CollisionPhase` has no suffix because both solvers use it.

---

## 1. Colliders

Attach `engine::BoxCollider2D`, `engine::CircleCollider2D`, or `engine::PolygonCollider2D` and `engine::Transform` to entities. Colliders are centered on the entity's world position. These types live in `engine`, not `engine::ecs`:

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

- `Static` does not integrate and is not moved by contacts. A collider with no `RigidBody2D` is static too. `angular_velocity` is stored and is not integrated, and the solver does not write it.
- `Kinematic` does `local position += velocity * fixed_delta_time` and is not moved by contacts. Two kinematics still tunnel. The solver does not change a kinematic velocity or `angular_velocity`. A kinematic's stored spin still contributes to the point velocity the solver reads, with inverse inertia 0, so it does not take torque.
- `Dynamic` adds gravity, then moves in world space. `velocity.xy` is world XY and `velocity.z` is world z. Inverse mass is `1 / max(mass, kMinDynamicMass)` (a non-positive or non-finite mass uses the minimum). After that linear write, a non-zero `angular_velocity * dt` yaws the body about world +Z. A zero turn is skipped, so an identity quaternion stays identity. A root does `rotation = rotation_z(angular_velocity * dt) * rotation`. A child writes that same world-Z turn back into its local rotation. The positional solver separates a dynamic body from non-trigger colliders and can change `angular_velocity`.

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

The crate falls, then rests with its bottom within `kContactSlop2D` (`1e-3`) of the floor. The solver runs 8 iterations (`kPositionIterations2D`). With the default restitution of 0 it does not bounce. A trigger still reports Enter, Stay, and Exit and never pushes. A layer mismatch is not solved. Resting overlap stays inside the slop and keeps reporting Stay. Once the shapes no longer overlap, the next step reports Exit. The event's penetration is the overlap before that correction, so a resting contact publishes the sunk pose from integration, not the separated pose.

A kinematic platform pushes a dynamic body along the contact normal and is not slowed. Its world velocity is the parent's world linear map (rotation and scale) times its local velocity. A root's `velocity.xy` is already world XY; the body's own rotation is not applied. Friction defaults to 0, so a floor that slides sideways under a crate does not drag it. Set friction on both colliders and that floor's tangential velocity pulls the crate.

Friction and restitution live on the collider, and the solver reads them without writing a clamped value back. A negative or non-finite friction is 0 for that step. Restitution is clamped to `[0, 1]` for that step. A pair uses `sqrt(fa * fb)` and `max(ra, rb)`. Set friction on **both** colliders: a zero on either side is a frictionless pair. `kBounceThreshold2D` is 1. An approach at that speed or slower (`v_rel_n >= -1`) is solved with restitution 0. Gravity `(0, -60)` adds exactly -1 to `velocity.y` each fixed step, so a body sitting on a floor does not bounce. A faster impact does. Particle `bounce` and `friction` are emitter fields. They do not read these collider fields. Particles still copy boxes and circles as axis-aligned shapes and do not see polygons or box yaw.

```cpp
world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};

auto ground = world.create();
world.emplace<engine::Transform>(ground, engine::Transform{.position = {0.f, -1.f, 0.f}});
world.emplace<engine::BoxCollider2D>(ground, engine::BoxCollider2D{
    .size = {8.f, 2.f, 1.f},
    .friction = 0.8f,
    .restitution = 1.f,
});

auto ball = world.create();
world.emplace<engine::Transform>(ball, engine::Transform{.position = {0.f, 3.f, 0.f}});
world.emplace<engine::RigidBody2D>(ball, engine::RigidBody2D{
    .type = engine::BodyType2D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::CircleCollider2D>(ball, engine::CircleCollider2D{
    .radius = 0.5f,
    .restitution = 1.f,  // pair restitution is max(ground, ball) = 1
});
```

The ball bounces. A crate on that same ground slides only when the crate has friction too. The pair's `mu` is `sqrt(0.8 * 0.5)`:

```cpp
auto sled = world.create();
world.emplace<engine::Transform>(sled, engine::Transform{.position = {0.f, 1.f, 0.f}});
world.emplace<engine::RigidBody2D>(sled, engine::RigidBody2D{
    .velocity = {10.f, 0.f, 0.f},
    .type = engine::BodyType2D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::BoxCollider2D>(sled, engine::BoxCollider2D{
    .size = {2.f, 2.f, 1.f},
    .friction = 0.5f,
});
```

A crate with friction 1 resting on a floor with friction 1, size `(2, 2)`, mass 1, `velocity.x = 10`, and its center at `y = 0.999` loses one unit of horizontal speed in one fixed step and starts to tumble. After that step `y` is still `0.999`, `velocity` is `(9, 0)`, `angular_velocity` is about `-1.48675`, and the angle is still 0 because the turn is applied on the next integrate. Friction 0 leaves `angular_velocity` at 0 and `velocity.x` at 10. The spin is about the contact, which sits below the center, so the sign is clockwise for a crate moving to the right.

```cpp
world.ctx<engine::Physics2DConfig>().gravity = {0.f, -60.f, 0.f};

auto ice = world.create();
world.emplace<engine::Transform>(ice, engine::Transform{.position = {0.f, -1.f, 0.f}});
world.emplace<engine::BoxCollider2D>(ice, engine::BoxCollider2D{
    .size = {8.f, 2.f, 1.f},
    .friction = 1.f,
});

auto tumbling = world.create();
world.emplace<engine::Transform>(tumbling, engine::Transform{.position = {0.f, 0.999f, 0.f}});
world.emplace<engine::RigidBody2D>(tumbling, engine::RigidBody2D{
    .velocity = {10.f, 0.f, 0.f},
    .type = engine::BodyType2D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::BoxCollider2D>(tumbling, engine::BoxCollider2D{
    .size = {2.f, 2.f, 1.f},
    .friction = 1.f,
});
```

A static polygon is a slope. Vertices are local, CCW, at most 8, and are not recentered: inertia and the shape are about the vertex-list origin, which is the body origin. Fewer than 3 vertices, a non-finite component, a zero-length edge, or a clockwise turn produces no contact. A collinear turn is allowed. This right triangle's outward hypotenuse is `(-1/sqrt(5), 2/sqrt(5))`. A dynamic box of size `(1, 1)` and mass 1 that starts at `(2.2, 1.85)` under gravity `(0, -60)` with friction 0 moves downhill.

```cpp
auto slope = world.create();
world.emplace<engine::Transform>(slope, engine::Transform{});
engine::PolygonCollider2D hill{};
hill.count = 3;
hill.vertices[0] = {0.f, 0.f};
hill.vertices[1] = {4.f, 0.f};
hill.vertices[2] = {4.f, 2.f};
world.emplace<engine::PolygonCollider2D>(slope, hill);

auto slider = world.create();
world.emplace<engine::Transform>(slider, engine::Transform{.position = {2.2f, 1.85f, 0.f}});
world.emplace<engine::RigidBody2D>(slider, engine::RigidBody2D{
    .type = engine::BodyType2D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::BoxCollider2D>(slider, engine::BoxCollider2D{.size = {1.f, 1.f, 1.f}});
```

Rotation contract:

- Scale is not applied to `size`, `radius`, or polygon vertices.
- A circle ignores spin. Its contact is the same as an unrotated circle.
- `BoxCollider2D` uses the world Z angle of the world matrix (the XY heading of its X axis). An angle of exactly `0` stays on the old axis-aligned manifold. Any other angle, and every `PolygonCollider2D`, uses the convex clipper. Polygon vertices are stored local and rotated once by that yaw.
- Kinematic and static bodies do not take torque. Only a dynamic body integrates `angular_velocity` and receives it back from the solver.
- Inertia is one number per dynamic body, from the first non-trigger collider in gather order: boxes, then circles, then polygons. Later solid colliders still generate contacts. They do not add another full mass into `I`. A box is `mass * (width^2 + height^2) / 12` using the full size. A circle is `0.5 * mass * radius^2`. A polygon uses the standard convex sum about the vertex-list origin. `I <= 0` leaves inverse inertia at 0.
- The event still publishes one point, `points[0]`: the deeper clip point, or the midpoint when the two depths are equal. The solver may use both points. Positional correction still uses only `points[0]` and the mass sum, not inertia.

Do not put `BodyType2D::Dynamic` on a predicted or controlled entity. Prediction replays `step`, and `run_physics_2d` is a second simulation of that entity.

Colliders are tested at the entity's **world position**. A collider on a child entity (see [Hierarchy](Hierarchy.md)) follows its parent. `size`, `radius`, and polygon vertices are not scaled. A box or polygon turns with the parent's world Z angle; a circle does not. A kinematic child's velocity is in its parent's space. A dynamic child is allowed: the solver writes the corrected world position through `set_world_position`, and its own yaw is written back into local `rotation`, so a translated or rotated parent keeps the contact in world space. The dynamic linear velocity itself stays world XY and does not include the parent's carry.

---

## 2. Physics Runs by Itself

The engine registers `run_physics_2d` and, immediately after it, `run_physics_3d` as systems on `Schedule::Fixed`, `Phase::Physics` for every world. You do not call either one, and you do not register them again. A second registration integrates velocities twice. Put your own fixed logic on `Phase::Game`, which runs after both:

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

`a` and `b` are ordered so `a < b`. `normal` is a unit XY vector from `a` toward `b`. Two boxes whose world angles are exactly 0 use the thinner overlap axis (X wins a tie; a zero center-delta on the chosen axis points along the positive axis, so coincident centers use +X). Two circles use the direction from center to center. A box at angle 0 and a circle use the closest-point direction (the nearest face when the circle center is inside the box). A rotated box or a polygon uses the convex clip: the least-penetrating face, with the normal pointing from `a` toward `b`. `penetration` is the overlap along that normal and is `>= 0`. A penetration of 0 is a touch and still an event. `point` is one world-space point, `points[0]` of the manifold: the deeper sample, or the midpoint when the two clip depths are equal. `Exit` sets `normal`, `penetration`, and `point` to zero and keeps the `is_trigger` value from the last overlapping step. A trigger still reports Enter, Stay, and Exit, still sets `is_trigger`, and still carries a normal, a penetration, and a point. Events are published before the solver, in gather order (boxes, then circles, then polygons). The solver does not change a kinematic velocity, so a kinematic character still cancels its own approach here.

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

## 4. 3D physics

`run_physics_3d` is a second solver, registered once by `register_simulation_systems`, immediately after `run_physics_2d`. Do not register it from game code. Types are not aliases of the 2D ones: `RigidBody3D`, `BoxCollider3D`, `SphereCollider3D`, `CapsuleCollider3D`, `Physics3DConfig`, and `CollisionEvent3D` in `<engine/ecs/physics_3d.h>`. `CollisionPhase` is the 2D enum.

`Physics3DConfig.gravity` starts at zero (`world.ctx<Physics3DConfig>()`). It is not serialized. The default `RigidBody3D` is kinematic, so a default component does not fall and contacts do not move it. Kinematic velocity is parent space and integrates `local.position += velocity * dt`. Dynamic velocity is world space: gravity, then `set_world_position`. Only a dynamic body integrates `angular_velocity` (radians per second about world axes), and only a dynamic body is written by the solver. A zero spin is skipped, so an identity quaternion stays identity. A root does `rotation = angleAxis(|ω| dt, ω/|ω|) * rotation`. A child writes that same world turn back into its local rotation.

Do not put `RigidBody2D` and `RigidBody3D` on one entity. An entity that has `RigidBody3D` is skipped by the 2D integrator, and a 2D pair is skipped when either side has `RigidBody3D`. 3D colliders are not visible to the 2D solver, and 2D colliders are not visible to the 3D solver.

Shapes ignore `Transform.scale` and use the world quaternion (the parent chain, not the scaled matrix axes). A capsule's `height` is the distance between its sphere centers along local Y. The total length is `height + 2 * radius`. `height == 0` is a sphere. Negative radius, negative height, or a negative box size produces no contact. Coincident centers use a `+X` normal.

Contacts are one point, the deepest. Two identity boxes use the center of the overlap box (X wins a tie, then Y, then Z), so a centered stack resting on a face has no torque. Any other box rotation uses SAT and one witness; a rotated face can rock. The solver runs 8 iterations (`kPositionIterations3D`) with slop `kContactSlop3D` (`1e-3`). Friction is `sqrt(fa * fb)` and restitution is `max(ra, rb)`. An approach of `kBounceThreshold3D` (1) or slower does not bounce, so gravity `(0, -60, 0)` does not bounce a body that is already at rest. The contact cache seeds the next step and is not added to velocity first. There is no broadphase and no sleep.

Inertia is one local diagonal per dynamic body, from the first non-trigger collider (boxes, then spheres, then capsules), rotated into a world inverse tensor. A box is `m/12 * (y²+z², z²+x², x²+y²)` with the full size. A sphere is `0.4 * m * r²` on every axis. A capsule is a uniform cylinder of length `height` plus two hemispheres; `height == 0` matches the sphere. Do not put `BodyType3D::Dynamic` on a predicted or controlled entity.

```cpp
world.ctx<engine::Physics3DConfig>().gravity = {0.f, -30.f, 0.f};

auto floor = world.create();
world.emplace<engine::Transform>(floor, engine::Transform{.position = {0.f, -1.f, 0.f}});
world.emplace<engine::BoxCollider3D>(floor, engine::BoxCollider3D{.size = {8.f, 2.f, 8.f}});

auto ball = world.create();
world.emplace<engine::Transform>(ball, engine::Transform{.position = {0.f, 2.f, 0.f}});
world.emplace<engine::RigidBody3D>(ball, engine::RigidBody3D{
    .type = engine::BodyType3D::Dynamic,
    .mass = 1.f,
});
world.emplace<engine::SphereCollider3D>(ball, engine::SphereCollider3D{.radius = 0.5f});
```

Read `CollisionEvent3D` the same way as `CollisionEvent2D`. `normal` points from `a` toward `b`. `Exit` zeros the normal, the penetration, and the point. A trigger still reports and is not solved.

---

## 5. Grid Collision and Block Picking

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
