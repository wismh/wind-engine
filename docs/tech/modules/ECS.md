# ECS

Homemade `ecs::World`: generational entities, sparse-set component pools, `ctx` resources, views, schedules, and double-buffered events. Camera and the physics probe sit on top of that world. There is no scene graph. Parenting is a separate `Parent` component with a derived `WorldTransform` (see Hierarchy); `Transform` has no parent field.

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
| `Frame` | `Input`, `Game`, `Transform`, `Bind`, `Audio`, `Render`, `UiRender` |

`Phase::Transform` is frame-only: the engine registers `update_world_transforms` there, after every `Game` system and before `Bind` and `Render`. `Phase::Physics` exists on the enum and is not a frame phase. Game code that needs a one-shot click uses `Schedule::Frame` and `Phase::Game`. Physics uses `fixed_delta_time`. Which systems the engine registers: [Runtime Loop](../architecture/Runtime%20Loop.md).

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

`Transform` is position, rotation, and scale. `rotation` is a unit `glm::quat`, identity by default. Scale defaults to `{1,1,1}`. `Transform` is always local: in world space for a root, in the parent's space for an entity with a `Parent`. `model_matrix(const Transform&)` (`include/engine/ecs/transform.h`) builds `T * mat4_cast(rotation) * S`.

Rotation helpers in `transform.h`:

| Function | Result |
| --- | --- |
| `rotation_z(radians)` | A turn about +Z. A 2D game sets rotation with it |
| `euler_xyz(vec3 radians)` | `Rx(x) * Ry(y) * Rz(z)`: Z applied first, then Y, then X |
| `angle_z(quat)` | The 2D angle back, in (-pi, pi]: the heading of the rotated +X axis in the XY plane. Exact inverse of `rotation_z`; 0 when that axis is on Z |

`Camera` (`include/engine/ecs/camera.h`, available in `namespace engine` and aliased in `namespace engine::ecs` along with `Projection`, `CameraProjection`, and `ActiveCamera`) looks down its local -Z with +Y up. `projection` is `Projection::Orthographic` (default) or `Projection::Perspective`. The defaults are the 2D camera, so a 2D game is unchanged.

| Field | Meaning |
| --- | --- |
| `ortho_size` | Orthographic: half-height in world units (default 10) |
| `fov_y` | Perspective: vertical field of view in radians (default 60 degrees) |
| `auto_aspect`, `aspect` | `auto_aspect` (default) rebuilds aspect from `WindowSize`; otherwise `aspect` |
| `near_clip`, `far_clip` | Default -1 and 1. Orthographic takes any range. Perspective needs `0 < near < far`, for example 0.1 and 1000 |

`projection_matrix` is `glm::ortho` from `ortho_size` or `glm::perspective(fov_y, aspect, near, far)`. A perspective near below `kMinPerspectiveNear` (0.01) is clamped up to it, a far not past near is clamped to `near + kMinPerspectiveNear`, and a warning is logged once per process. Clamping is not an error: a camera switched to perspective with the 2D clip defaults still draws.

`ActiveCamera` is a `ctx` resource holding one entity. `run_render` clears bound and `UiCanvas` window command buffers at the start of `Phase::Render`; when `ctx<BoundWindows>().ids` is empty, `run_render` returns before the camera check.

When the list is not empty, it returns when nothing is drawable. A drawable scene (any `Renderable`, `Sprite`, or `ParticleEmitter`) with an invalid or default `ActiveCamera` entity is fatal.

Fatal is also a live entity that lacks `Camera` or `Transform`. Either way that frame draws no world commands. A world with only UI needs no camera. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again. The view matrix is the inverse of the camera entity's world matrix (`ecs::world_matrix`), so a camera with a rotation or a `Parent` works. Each bound window gets one `render::CmdSetView` before its draws, with the same view matrix and camera position (the translation of that world matrix). Its projection is `window_size_for` for that id (`Presentation.sizes`). For rendering an arbitrary `World` into a sub-rectangle of a window with an explicit camera (`WorldViewportRender`), call `render_world_viewport` (`include/engine/ecs/systems.h`; see [Render](Render.md#world-draw)).

Light components (`render::DirectionalLight`, `render::AmbientLight`) are plain data on entities; a directional light is aimed by its `Transform::rotation` and follows a `Parent`. Rules: [Render](Render.md#lights).

`view_matrix`, `screen_to_ray`, `screen_to_world` and `world_to_screen` take the camera's world matrix (`const glm::mat4&`, from `ecs::world_matrix(world, camera_entity)`), not its `Transform`. The last three also take the `Camera` and a `WindowSize`. Screen positions are window pixels, origin top-left, +Y down.

| Function | Result |
| --- | --- |
| `screen_to_ray(screen, camera, camera_world, window)` | A `Ray` (`include/engine/ecs/ray.h`: `origin`, unit `direction`) from the near plane away from the camera. Orthographic: every ray is parallel to the camera's forward. Perspective: through the pixel |
| `screen_to_world(screen, camera, camera_world, window)` | Orthographic: the point under the pixel halfway through the clip range (the camera's z for the default -1 and 1). Perspective: where `screen_to_ray` meets the world plane z = 0, which is what 2D-style picking expects; the ray's near-plane origin when the ray is parallel to that plane or the plane is behind the camera |
| `world_to_screen(world_pos, camera, camera_world, window)` | The pixel the point projects to, either projection |

`PointerTarget` hit-testing intersects the cursor's `screen_to_ray` with the plane z of each target, so it works under a perspective camera. For an unrotated orthographic camera that is the same point as before.

## Pointer targets

`PointerTarget` is a component, not a callback: the entity's rectangle for the mouse. The input system reports hover, press, and click as `PointerTargetEvent` and as `hovered` / `pressed`, and counts the target for click-through itself. See [UI Input](../features/UI%20Input.md#pointer-targets).

## Hierarchy

`include/engine/ecs/hierarchy.h`, `src/ecs/hierarchy.cpp`. Not a scene graph: three components and free functions over `World`.

| Type | Role |
| --- | --- |
| `Parent{Entity entity}` | Plain data. The entity this one hangs from. Replicated state, together with `Transform`. |
| `Children{std::vector<Entity> entities}` | Derived index, the reverse of `Parent`. Only `set_parent`, `clear_parent` and destroy write it. |
| `WorldTransform{glm::mat4 matrix}` | Derived world matrix. Only `update_world_transforms` writes it. Never replicated. An entity gets one only while it has a `Parent`; roots use `model_matrix(Transform)`. |

The components live in namespace `engine` (like `Transform`). The functions that take a `World` live in `engine::ecs`.

Functions:

- `set_parent(world, child, parent, KeepWorld = No)` returns false and changes nothing for an invalid entity, `child == parent`, a cycle (`parent` is a descendant of `child`), or a result deeper than `kMaxHierarchyDepth` (32, root = 0, the child's own subtree counts). Setting the current parent again succeeds and does nothing. An entity without `Transform` is allowed as parent or child and counts as identity. Reparenting moves the child between `Children` lists.
- `KeepWorld::Yes` recomputes the child's local `Transform` from the exact world matrices so the world matrix does not change (`Transform` is created if the child had none). The matrix is decomposed into position, a rotation quaternion, and scale (a mirrored matrix puts the sign into `scale.x`). Skew caveat: a non-uniform scale above a rotated child gives a local matrix `Transform` cannot represent; the world position is kept exactly, rotation and scale are the best fit: the local X axis is kept, Y is made orthogonal to it, Z is their cross product, and scale is the length of each axis.
- `clear_parent(world, child, KeepWorld = No)` removes `Parent` and `WorldTransform` and takes the child out of its parent's `Children`.
- `world_matrix(world, e)`: `WorldTransform` if present, else `model_matrix(Transform)`, else identity. `compute_world_matrix` is the slow exact path that walks the `Parent` chain and ignores `WorldTransform`. `world_position` reads `world_matrix`. `set_world_position(world, e, p)` converts `p` into the parent's space (exact chain) and writes the local `Transform.position`.
- `update_world_transforms(world)` recomputes the whole forest every call, parents before children, idempotent. There are no dirty flags: `Transform` is a public struct that code mutates directly. It starts from the roots (entities with `Children` and no `Parent`), writes `WorldTransform` on every entity that has a `Parent`, and removes `WorldTransform` from entities that no longer have one. Deleting a `WorldTransform` and updating again gives the same matrix. Callers: the `Frame` / `Transform` system, `run_particles` at its start (it runs in `Game`, before `Transform`), and `run_physics_2d` after integration. Anything else that needs fresh matrices in between calls it too.

Destroying a parent destroys all its descendants (iterative, safe for deep chains), and destroying a child removes it from its parent's `Children`. `World::destroy_immediate` does this through `Children`, a deliberate small coupling of `world.cpp` to `hierarchy.h`. `destroy` inside a view is still deferred: the parent and its descendants stay alive and valid until the view ends, and then the cascade runs.

A `Parent` whose entity is dead can only come from hand-made data (the cascade prevents it). `update_world_transforms` logs one warning, removes that `Parent`, and the entity is a root from then on. `compute_world_matrix` stops its walk at a dead parent without logging.

Staleness contract: the engine refreshes `WorldTransform` in `Phase::Transform` (Frame) and inside `run_physics_2d` / `run_particles`. A `Game` system sees the matrices of the previous frame's `Transform` phase plus whatever `run_particles` refreshed, so it should read `compute_world_matrix` or call `update_world_transforms` itself when it needs the result of an edit made this frame. `run_render` needs no refresh, because `Transform` precedes `Render`. `WorldTransform` is only as fresh as the last `update_world_transforms`. After a local `Transform` edit, `set_parent` or `set_world_position`, `world_matrix` still returns the old value until the next update; use `compute_world_matrix` for an exact answer now. `Parent` must be set through `set_parent`; a `Parent` that its parent's `Children` does not list is not visited by the update. `RigidBody2D.velocity` of a child is in the parent's space, because `run_physics_2d` integrates the local `Transform.position`.

### Multiplayer readiness

Replicate `Transform` and `Parent` only. `WorldTransform` and `Children` are derived and rebuilt on the receiving side. `Entity` handles (index + generation) are not stable across machines, so the net layer needs its own stable id (a `NetId` component and a map to local entities) and has to translate `Parent.entity` through it. That layer is not part of the engine ECS.

## Physics

The physics is 2D: `include/engine/ecs/physics_2d.h`. Its components, event and system carry a `2D` suffix (`RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `CollisionEvent2D`, `run_physics_2d`) so a 3D physics can add its own types without a clash. `CollisionPhase` (`Enter`, `Stay`, `Exit`) has no dimension and keeps its name for both. `render::ParticleCollider2D` is the XY-only copy of these colliders that `run_particles` hands to `update_emitter`.

`run_physics_2d` is the Fixed / Physics system.

1. Integrate `Transform.position += RigidBody2D.velocity * fixed_delta_time`.
2. Call `update_world_transforms`, then collect `BoxCollider2D` and `CircleCollider2D` at their **world position** (`world_position`; XY overlap, centered on it). A collider on a child follows its parent. Colliders ignore scale and rotation, as before: `size` and `radius` are world units.
3. A pair overlaps when each layer bit hits the other's mask.
4. Send `CollisionEvent2D` with `Enter`, `Stay`, or `Exit`. `is_trigger` is true if either collider is a trigger.

`RigidBody2D.velocity` moves the local `Transform.position`, so the velocity of a child is in its parent's space. There is no contact solver, no response impulse, and no Z test. Overlap state is `ctx<PhysicsOverlapState>` in `src/ecs/physics_2d.cpp`.

## World serialization

`include/engine/ecs/world_io.h`, `include/engine/ecs/world_asset.h`, `include/engine/ecs/world_serializer.h`.

`.world` TOML files (`importer = "world"`) parse into a `WorldAsset` (`version = 1`, optional `active_camera`, and `[[entities]]` with `id`, optional `parent`, and `[entities.<component>]` tables).

- `WorldSerializer` (available via `world.ctx<WorldSerializer>()`) comes pre-populated with built-in engine components: `transform`, `camera`, `sprite`, `sprite_animator`, `renderable`, `directional_light`, `ambient_light`, `rigid_body_2d`, `box_collider_2d`, `circle_collider_2d`, and `pointer_target`.
- Game-specific components marked `struct WIND_COMPONENT MyComponent { ... };` are registered by calling `assets::register_game_components(world.ctx<WorldSerializer>())` from the generated `<component_io.h>` header (see [Asset Codegen](../build/Asset%20Codegen.md#component-codegen)), or by calling `GameModule::register_components(serializer)` (`wind_register_game_components`).
- `instantiate_world(world, asset, &assets, serializer, unknown_mode)` and `load_world_from_toml` spawn entities in two passes (entities and `EntityName` first so `ecs::Entity` cross-references resolve in any order, then hierarchy and components), set `ActiveCamera` when `active_camera` is non-empty, and call `update_world_transforms(world)`. On any error, newly created entities are rolled back.
- `UnknownComponentMode::Reject` (default) fails with `WorldErrorKind::UnknownComponent` when a component key is not registered in the active `WorldSerializer`. `UnknownComponentMode::Preserve` stores unregistered component tables in `UnknownComponents` (`std::map<std::string, FieldTable, std::less<>> components`) on the entity so the editor can inspect and round-trip game components without losing data when the game module is not loaded.
- `capture_world_asset` and `save_world_to_toml` snapshot all alive entities (`World::alive_entities()`), their registered components, and any preserved `UnknownComponents` entries back to a `WorldAsset` or TOML string.

## Public headers

- `include/engine/ecs/world.h`
- `include/engine/ecs/world.inl`
- `include/engine/ecs/world_io.h`
- `include/engine/ecs/world_asset.h`
- `include/engine/ecs/world_serializer.h`
- `include/engine/ecs/entity.h`
- `include/engine/ecs/schedule.h`
- `include/engine/ecs/events.h`
- `include/engine/ecs/transform.h`
- `include/engine/ecs/hierarchy.h`
- `include/engine/ecs/camera.h`
- `include/engine/ecs/pointer_target.h`
- `include/engine/ecs/physics_2d.h`
- `include/engine/ecs/systems.h`

## Tests

`tests/ecs_test.cpp`, `tests/hierarchy_test.cpp`, `tests/hierarchy_systems_test.cpp`, `tests/events_test.cpp`, `tests/camera_test.cpp`, `tests/physics_2d_test.cpp`, `tests/pointer_target_test.cpp`, `tests/world_serializer_test.cpp`.

## See also

- [Principles](../architecture/Principles.md)
- [Core](Core.md)
- [Materials and Sort](../features/Materials%20and%20Sort.md)
