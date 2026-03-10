# ECS

Homemade `ecs::World`: generational entities, sparse-set component pools, `ctx` resources, views, schedules, and double-buffered events. Camera and the physics probe sit on top of that world. There is no scene graph and no `Transform` parent.

## Entities

`Entity` is an index plus a generation (`include/engine/ecs/entity.h`).

- `create` recycles a free index and bumps the generation.
- `destroy` during a view is deferred until the view depth returns to zero (`src/ecs/world.cpp`).
- `valid` is false after destroy. `try_get` returns null. `get` asserts.

Pools are a sparse index, a packed entity list, and a dense component array (`include/engine/ecs/world.inl`).

## Views and resources

`view<Ts...>()` requires at least one component. Iteration walks the smallest pool and skips entities missing any other type. `each` passes components by reference. A view holds `view_depth_` so destroy stays deferred.

`ctx<T>()` default-constructs one resource of type `T` per world. Engine code stores `Time`, `BoundWindows`, `ActiveCamera`, event cursors, and the localization `Catalog` this way. `ApplicationState` lives on `Worlds`, one per process. `WindowSizes`, `MouseConsumed`, and the pointer maps live on the process `Presentation`. `bind_presentation` stores a non-owning `ctx<Presentation*>`.

`window_size_for` reads `Presentation.sizes`. A hit inserts `Presentation.mouse`. `sync_frame` reads that same object. `ctx<ui::WindowSizes>()` and `ctx<ui::MouseConsumed>()` are separate. Resize, backfill, `window_size_for`, the hit-test, and `sync_frame` use the `Presentation` members.

## Schedules

`add_system(schedule, phase, fn)` appends a `std::function<void(World&)>`. `run` walks that schedule's phases in enum order, but only the phases listed in `kFixedPhases` or `kFramePhases`.

| Schedule | Phases |
| --- | --- |
| `Fixed` | `Physics`, `Game` |
| `Frame` | `Input`, `Game`, `Bind`, `Audio`, `Render`, `UiRender` |

`Phase::Physics` exists on the enum and is not a frame phase. Game code that needs a one-shot click uses `Schedule::Frame` and `Phase::Game`. Physics uses `fixed_delta_time`. Which systems the engine registers: [Runtime Loop](../architecture/Runtime Loop.md).

## Events

`Events<T>` keeps two generations (`previous_` and `current_`). `World::flush_events` calls `update` on every queue: the previous generation is dropped and current becomes previous.

`EventWriter` sends into the live queue. Two readers:

| Constructor | What it sees |
| --- | --- |
| `EventReader(Events)` or `EventReader(World)` | every event still in the two-generation buffer, every time. A system that builds this every frame sees an event on the send frame and the next one |
| `EventReader(Events, EventCursor)` or `EventReader(World, cursor)` | events since `cursor.next_id`, then advances the cursor immediately. A cursor that fell behind the buffer is clamped; those events are gone |

The usual per-frame cursor is `ctx<EventCursor<T>>()`. A second independent reader holds its own `EventCursor`.

`flush_worlds` runs at the start of `GameLoop::tick` and `Host::tick`, before simulate. It flushes every world. `reentrant_tick` does not flush.

## Transform and camera

`Transform` is position, rotation (radians, applied X then Y then Z), and scale. All default to identity scale `{1,1,1}`. No parent.

`Camera` is orthographic. `ortho_size` is the half-height in world units (default 10). `auto_aspect` rebuilds aspect from `WindowSize`. `near_clip` is -1, `far_clip` is 1.

`ActiveCamera` is a `ctx` resource holding one entity. When `ctx<BoundWindows>().ids` is empty, `run_render` returns before a command-buffer clear and before the camera check.

When the list is not empty, it clears each bound window's buffer, then returns when nothing is drawable. An invalid or default camera returns after that clear and does not call the fatal hook.

Fatal is only a live entity that lacks `Camera` or `Transform`, and that frame also draws no world commands. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again. Each bound window uses the same view matrix. Its projection is `window_size_for` for that id (`Presentation.sizes`).

`screen_to_world` and `world_to_screen` take that camera, its transform, and a `WindowSize`.

## Physics

`run_physics` is the Fixed / Physics system.

1. Integrate `Transform.position += RigidBody.velocity * fixed_delta_time`.
2. Collect `BoxCollider` and `CircleCollider` (XY overlap, centered on position).
3. A pair overlaps when each layer bit hits the other's mask.
4. Send `CollisionEvent` with `Enter`, `Stay`, or `Exit`. `is_trigger` is true if either collider is a trigger.

There is no contact solver, no response impulse, and no Z test. Overlap state is `ctx<PhysicsOverlapState>` in `src/ecs/physics.cpp`.

## Public headers

- `include/engine/ecs/world.h`
- `include/engine/ecs/world.inl`
- `include/engine/ecs/entity.h`
- `include/engine/ecs/schedule.h`
- `include/engine/ecs/events.h`
- `include/engine/ecs/transform.h`
- `include/engine/ecs/camera.h`
- `include/engine/ecs/physics.h`
- `include/engine/ecs/systems.h`

## Tests

`tests/ecs_test.cpp`, `tests/events_test.cpp`, `tests/camera_test.cpp`, `tests/physics_test.cpp`.

## See also

- [Principles](../architecture/Principles.md)
- [Core](Core.md)
- [Materials and Sort](../features/Materials and Sort.md)
