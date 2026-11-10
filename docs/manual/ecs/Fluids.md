# 2D Fluids

`engine::Fluid2D` (`<engine/ecs/fluid_2d.h>`) is a liquid made of particles: water poured into a level, a flooding cave, lava in a pit. The engine simulates it (Position Based Fluids) on the fixed clock after physics, once you turn fluids on for the world; you spawn particles, place colliders that hold them, and draw the positions however you like. The engine does not render a fluid.

---

## 1. Turning fluids on

Fluids are off by default: a `Fluid2D` on a world without them keeps its particles and never moves, with no warning. Call `install_fluid_2d` once per world that has liquid, at setup:

```cpp
#include <engine/ecs/fluid_2d.h>

MyGame::MyGame(const engine::EngineServices& services)
    : engine::GameBase(services.worlds) {
    engine::install_fluid_2d(world(), &services.jobs);   // the worker pool, for parallel solver passes
}
```

- The constructor or `on_start` both work. Either way `run_fluid_2d` runs on `Schedule::Fixed`, `Phase::Physics` after `run_physics_2d` and `run_physics_3d`, so the fluid sees where bodies moved this tick. From `on_start` it also runs after any `Phase::Physics` system you added before the call; keep your own fixed logic on `Phase::Game`.
- Calling it again does not add a second step; it only replaces the job system. Do not register `run_fluid_2d` yourself.
- Without `jobs` (or with `nullptr`) the solver runs on the main thread. The result is the same bit for bit.
- A world you add later (`services.worlds.add()`) needs its own call.

## 2. A tank of water

```cpp
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/transform.h>

// A container: SDF terrain with a hollow carved out of it.
auto tank = world.create();
world.emplace<engine::Transform>(tank, engine::Transform{});
engine::SdfCollider2D walls{.field = engine::SdfField2D{{.cell_size = 0.1f, .band = 0.4f}}};
(void) walls.field.fill_polygon(outer_rect);
(void) walls.field.carve_polygon(inner_rect);
world.emplace<engine::SdfCollider2D>(tank, std::move(walls));

// The water: parameters first, then particles.
auto water = world.create();
world.emplace<engine::Fluid2D>(water, engine::Fluid2DParams{.radius = 0.05f});
world.get<engine::Fluid2D>(water).spawn_box({0.f, 0.f}, {1.2f, 2.4f});   // a column that collapses

world.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};       // the fluid uses physics gravity
```

- Positions are world XY. The fluid entity needs no `Transform`.
- `spawn(position, velocity)` adds one particle (a hose, a leak); it returns false at `max_particles`. `spawn_box` fills a rectangle at the rest spacing (two radii) so the block starts at rest. `clear()` empties the fluid.

## 3. Drawing it

Add a `FluidRender2D` to the fluid's entity and the engine draws it: a soft disc per particle, four radii wide, so the drops blend into one body of water. Give it a `color`, a `foam_color` with a `foam_speed` for white water where it moves fast, a `layer` to sort it with your sprites, or your own `texture` or `material` for the drops.

```cpp
world.emplace<engine::render::FluidRender2D>(pool, engine::render::FluidRender2D{
    .color = {0.2f, 0.5f, 1.0f, 0.8f},
    .foam_speed = 3.0f,  // white at 3 units per second
    .layer = 5,
});
```

It is the simple look: discs, not a surface with a sharp edge. For anything else, read `positions()` every frame and draw them with your own sprites, a particle emitter you fill yourself, or a custom mesh. `velocities()` and `densities()` (1 at rest, lower at the surface, higher where it is squeezed) are there for colour, foam or splashes. The order of particles never changes, so index `i` is the same drop from tick to tick.

## 4. What holds the fluid

- `SdfCollider2D` terrain, static or on a Kinematic body. Carve a hole and the water drains through it; no mark is needed for the fluid (mark the collider for rigid bodies as usual).
- `BoxCollider2D`, `CircleCollider2D` and `PolygonCollider2D` on anything: static walls, a kinematic paddle that sweeps water ahead of it, a dynamic crate that sinks and pushes water aside.
- `CompoundCollider2D` terrain (tiles, marching-squares cells), static or on a Kinematic body. Only the parts near the water cost anything.
- The usual layer rule: the fluid's `mask` must include the collider's `layer`, and the collider's `mask` the fluid's `layer`.

Water pushes back on Dynamic bodies: a crate lighter than the water floats, a heavier one sinks, a thrown rock splashes and slows down, a tilted plank turns flat. How heavy the water is comes from `density` (mass per unit area, default 1, in the units of `RigidBody2D::mass`: a 1 x 1 box of mass 0.5 floats half under water). This works for box, circle and polygon colliders on the body's own entity. Bodies much lighter than the water (below about 0.15 of its density) bob at the surface instead of settling. Set `density = 0` for water that only gets pushed around. No `CollisionEvent2D` comes from the water.

Two fluids that meet mix by weight: give each its own `density` and the lighter one floats on the heavier (oil on water), a heavier one sinks (mud). Fluids whose `mask` leaves out the other's `layer` pass through each other instead. Where two fluids touch, a `FluidContactEvent2D` arrives every tick with how many particles touch and where (`center`, `min`, `max`), for reactions like lava meeting water:

```cpp
for (const engine::FluidContactEvent2D& e : engine::ecs::EventReader<engine::FluidContactEvent2D>{world, cursor_}) {
    if ((e.a == lava_ && e.b == water_) || (e.a == water_ && e.b == lava_)) {
        // Harden the meeting zone: fill the terrain there.
        (void)world.get<engine::SdfCollider2D>(terrain_).field.fill_circle(e.center, 0.3f);
        engine::mark_static_colliders_dirty_2d(world, terrain_);  // for the rigid bodies
    }
}
```

Fluids that touch are stepped together and share the largest `smoothing_length`, `substeps` and `iterations` of the group, so give fluids that will meet the same `radius`.

```cpp
engine::Fluid2DParams water{.radius = 0.05f, .density = 1.0f};
// A 0.5 x 0.5 crate of mass 0.1 (density 0.4): floats with 40 percent of it under water.
world.emplace<engine::RigidBody2D>(crate, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = 0.1f});
```

## 5. Tuning

| Field | Default | What it does |
| --- | --- | --- |
| `radius` | 0.05 | particle size: how far it stays from walls, half the rest spacing |
| `smoothing_length` | 0 (6 radii) | how far a particle feels its neighbours |
| `viscosity` | 0.1 | 0 sloshes freely; higher is thicker (honey, lava) |
| `iterations`, `substeps` | 4, 2 | more is stiffer and less compressible, and costs linearly |
| `friction`, `restitution` | 0.1, 0 | against walls: slide damping and bounce |
| `gravity_scale` | 1 | 0 for a floating blob, negative for a liquid that falls up |
| `max_particles` | 16384 | `spawn` refuses beyond it |
| `density` | 1 | mass per unit area: what floats in it ([section 4](#4-what-holds-the-fluid)) |
| `cohesion` | 0 (off) | surface tension: drops hold together and round off. 4 to 16 keeps a pool at rest; 16 to 64 holds falling drops tighter, and a pool shimmers a little at 64 |
| `vorticity` | 0 (off) | puts back the swirl the solver damps, so eddies last. 0.02 to 0.1 is safe; from about 0.2 a vortex speeds up on its own |

Leave `rest_density`, `relaxation` and `tensile_strength` at their defaults unless you know Position Based Fluids: they are in the solver's normalized units, and a `tensile_strength` above about 0.01 makes the fluid swell.

Fast fluid does not leak through thin walls: a particle that moves more than half its radius in a substep is swept along its step and stops at the first box, circle or terrain surface on the way. An `SdfCollider2D` wall thinner than one field cell exists only where it covers a sample of the field, so keep terrain walls at least a cell thick (or straddling a sample). A particle pushed deep into terrain (deeper than the field's `band`) stays there.

## 6. Taps, drains and how full a pool is

A tap is a `FluidSource2D` on the fluid: a box or circle where particles appear at `rate` per second with a `velocity`. A drain is a `FluidDrain2D`: particles that end a tick inside it disappear. A sensor is a `FluidSensor2D`: it watches how full a region is and sends a `FluidSensorEvent2D` when it fills past `threshold` and again when it empties.

```cpp
engine::Fluid2D& water = world.get<engine::Fluid2D>(pool);
water.sources.push_back(engine::FluidSource2D{
    .region = {.shape = engine::FluidRegion2D::Shape::Box, .center = {2.f, 3.f}, .half_extents = {0.2f, 0.05f}},
    .rate = 200.f,  // particles per second
    .velocity = {0.f, -2.f},
});
water.drains.push_back(engine::FluidDrain2D{
    .region = {.shape = engine::FluidRegion2D::Shape::Circle, .center = {6.f, 0.f}, .radius = 0.3f}});
water.sensors.push_back(engine::FluidSensor2D{
    .region = {.center = {2.f, 0.5f}, .half_extents = {1.f, 0.5f}},
    .threshold = 0.8f,  // 80 percent full
});

// In a Fixed system after the fluid, with a cursor the system keeps:
for (const engine::FluidSensorEvent2D& e : engine::ecs::EventReader<engine::FluidSensorEvent2D>{world, cursor_}) {
    if (e.full) {
        open_the_gate();
    }
}
```

`drains[i].drained` counts what a drain took. To ask about the water directly, `water.query(region)` or `water.query(polygon)` give how many particles are inside, the area they cover and their average velocity, and `water.is_wet(point)` says whether water touches a point. Asking many times a tick is cheap.

Taps, drains and sensors are saved with the world: their settings, not how much they have emitted or drained.

## 7. Cost and saving

About 4.6 us per particle per tick on one thread with the defaults, and it scales across the job workers: 4,000 particles cost about 3 ms a tick on a 12-thread desktop, 16,000 about 10 ms. Results are the same for any number of workers.

`fluid_2d` is saved with the world: its parameters, taps, drains and sensors. The water itself is saved only when you set `save_particles = true` on the fluid; then a loaded game has the same water, to the bit. Leave it off for large fluids you can spawn again (a few megabytes per 100,000 particles). Over the network, every player simulates the fluid: see [Fluids over the network](#8-fluids-over-the-network).

To place a pool in a scene, add a child entity to the fluid's entity with a Transform and a `FluidBox2D` (`size`, optional `velocity`): the fluid fills that box with water the first time it steps. In the editor the World tab shows the box and the water it will hold, and draws the particles of every fluid; move the box with the gizmo and size it in the Inspector.

The fluid is built to give the same bits on every platform the engine supports, not only for any number of workers, so two peers that feed it the same inputs keep the same water (checked so far on Windows with MSVC and clang, and on Web). `fluid.state_hash()` is a 64-bit hash of its particles: compare it between peers to catch a desync.

## 8. Fluids over the network

The fluid is not sent in snapshots; every peer runs it, and the server sends what changes it. Give the fluid and its terrain the same `engine::net::FluidSyncId2D` on every peer (the level file does it), then:

```cpp
// Server, in on_start after install_net_server (instead of install_fluid_2d):
engine::net::install_fluid_sync_server_2d(world(), transport_);
// Change the water and the terrain only through inputs:
engine::net::queue_fluid_input_2d(world(), {.kind = engine::net::FluidInput2D::Kind::CarveCircle,
                                            .target = kTerrain, .a = hit, .value = 1.5f});

// Client, after install_net_client:
engine::net::install_fluid_sync_client_2d(world(), transport_);
```

A player who joins late gets the water and the terrain as they are, then follows along. The client's water moves with the bodies it shows, a little behind the server like everything else. If a client ever disagrees with the server, it notices within half a second and gets the server's water again.

---

## Next Steps

- Build terrain for it in [Physics & Collisions](Physics-and-Collisions.md#destructible-terrain-that-collides-sdfcollider2d).
- Draw the drops with your own sprites or [Particles](../rendering/Particles.md).
