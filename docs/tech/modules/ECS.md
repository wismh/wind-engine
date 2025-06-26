---
tags: [module]
---

# ECS

Homemade `ecs::World`: generational entities, components, `ctx` resources, views, schedules, event queues, camera, AABB/circle physics probe. Not EnTT. Not a scene graph.

## Capabilities

- `Entity` with generation; `try_get` after destroy is empty; deferred destroy during iteration ([[include.engine.ecs.world.h]]).
- `Schedule` / `Phase` (Fixed vs Frame). Gameplay/physics on `fixed_delta_time`; one-shot clicks on Frame `Phase::Game` ([[include.engine.ecs.schedule.h]]).
- Double-buffered `Events<T>` and `EventCursor<T>` ([[include.engine.ecs.events.h]]).
- Flat `Transform` (no parent / world-matrix chain) ([[include.engine.ecs.transform.h]]).
- Orthographic `Camera`; world rendering is `kPrimaryWindow` only ([[include.engine.ecs.camera.h]], [[features/Windowing]]).
- `BoxCollider` / `CircleCollider` + velocity integration; overlap events, not a contact solver ([[include.engine.ecs.physics.h]]).
- Engine systems registered once ([[include.engine.ecs.systems.h]]).

## Public headers

- [[include.engine.ecs.world.h]]
- [[include.engine.ecs.entity.h]]
- [[include.engine.ecs.schedule.h]]
- [[include.engine.ecs.events.h]]
- [[include.engine.ecs.transform.h]]
- [[include.engine.ecs.camera.h]]
- [[include.engine.ecs.physics.h]]
- [[include.engine.ecs.systems.h]]

## See also

- [[architecture/Principles]]
- [[modules/Core]]
