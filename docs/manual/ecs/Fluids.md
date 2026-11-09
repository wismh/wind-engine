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

Read `positions()` every frame and draw them with your own sprites, a particle emitter you fill yourself, or a custom mesh. `velocities()` and `densities()` (1 at rest, lower at the surface, higher where it is squeezed) are there for colour, foam or splashes. The order of particles never changes, so index `i` is the same drop from tick to tick.

## 4. What holds the fluid

- `SdfCollider2D` terrain, static or on a Kinematic body. Carve a hole and the water drains through it; no mark is needed for the fluid (mark the collider for rigid bodies as usual).
- `BoxCollider2D`, `CircleCollider2D` and `PolygonCollider2D` on anything: static walls, a kinematic paddle that sweeps water ahead of it, a dynamic crate that sinks and pushes water aside.
- `CompoundCollider2D` terrain (tiles, marching-squares cells), static or on a Kinematic body. Only the parts near the water cost anything.
- The usual layer rule: the fluid's `mask` must include the collider's `layer`, and the collider's `mask` the fluid's `layer`.

Coupling is one way: colliders push the fluid, the fluid never pushes back. A crate does not float and gets no `CollisionEvent2D` from the water. Two fluids pass through each other.

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

Leave `rest_density`, `relaxation` and `tensile_strength` at their defaults unless you know Position Based Fluids: they are in the solver's normalized units, and a `tensile_strength` above about 0.01 makes the fluid swell.

Fast fluid does not leak through thin walls: a particle that moves more than half its radius in a substep is swept along its step and stops at the first box, circle or terrain surface on the way. An `SdfCollider2D` wall thinner than one field cell exists only where it covers a sample of the field, so keep terrain walls at least a cell thick (or straddling a sample). A particle pushed deep into terrain (deeper than the field's `band`) stays there.

## 6. Cost and saving

About 4.6 us per particle per tick on one thread with the defaults, and it scales across the job workers: 4,000 particles cost about 3 ms a tick on a 12-thread desktop, 16,000 about 10 ms. Results are the same for any number of workers.

`fluid_2d` is saved with the world, parameters only: a loaded fluid is empty, so spawn into it after loading. Fluids are not replicated over the network.

The fluid is built to give the same bits on every platform the engine supports, not only for any number of workers, so two peers that feed it the same inputs keep the same water (checked so far on Windows with MSVC and clang, and on Web). `fluid.state_hash()` is a 64-bit hash of its particles: compare it between peers to catch a desync.

---

## Next Steps

- Build terrain for it in [Physics & Collisions](Physics-and-Collisions.md#destructible-terrain-that-collides-sdfcollider2d).
- Draw the drops with your own sprites or [Particles](../rendering/Particles.md).
