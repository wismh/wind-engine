# ECS

Homemade `ecs::World`: generational entities, sparse-set component pools, `ctx` resources, views, schedules, and double-buffered events. Camera and the 2D and 3D physics solvers sit on top of that world. There is no scene graph. Parenting is a separate `Parent` component with a derived `WorldTransform` (see Hierarchy); `Transform` has no parent field.

## Entities

`Entity` is an index plus a generation (`include/engine/ecs/entity.h`).

- `create` recycles a free index and bumps the generation.
- `destroy` during a view is deferred until the view depth returns to zero (`src/ecs/world.cpp`).
- `valid` is false after destroy. `try_get` returns null. `get` asserts.

Pools are a sparse index, a packed entity list, and a dense component array (`include/engine/ecs/world.inl`).

## Views and resources

`view<Ts...>()` requires at least one component. Iteration walks the smallest pool and skips entities missing any other type. `each` passes components by reference. A view holds `view_depth_` so destroy stays deferred.

`structure_version<T>()` is a structural signal, not change detection. It changes whenever a `T` is added to or removed from any entity, destroy included. Every pool is stamped from one counter per `World`, so a value never repeats within a world (destroying everything and building the same content again gives new values), and 0 means no `T` was ever added. Editing a `T` in place through `get` / `try_get` / a view does not change it. 2D physics uses it to notice collider adds and removes without walking the colliders.

`ctx<T>()` default-constructs one resource of type `T` per world. Engine code stores `Time`, `BoundWindows`, `ActiveCamera`, event cursors, and the localization `Catalog` this way. `ApplicationState` lives on `Worlds`, one per process. `WindowSizes`, `MouseConsumed`, and the pointer maps live on the process `Presentation`. `bind_presentation` stores a non-owning `ctx<Presentation*>`.

`window_size_for` reads `Presentation.sizes`. A hit inserts `Presentation.mouse`. `sync_frame` reads that same object. `ctx<ui::WindowSizes>()` and `ctx<ui::MouseConsumed>()` are separate. Resize, backfill, `window_size_for`, the hit-test, and `sync_frame` use the `Presentation` members.

## Schedules

`add_system(schedule, phase, fn)` appends a `std::function<void(World&)>`. `run` walks that schedule's phases in enum order, but only the phases listed in `kFixedPhases` or `kFramePhases`.

| Schedule | Phases |
| --- | --- |
| `Fixed` | `NetReceive`, `Physics`, `Game`, `NetSend` |
| `Frame` | `NetReceive`, `Input`, `Game`, `Transform`, `Bind`, `Audio`, `Render`, `UiRender` |

`Phase::Transform` is frame-only: the engine registers `update_world_transforms` there, after every `Game` system and before `Bind` and `Render`. `Phase::Physics` exists on the enum and is not a frame phase. `NetReceive` and `NetSend` hold the network systems, so a server tick brackets the whole fixed step and a client sees replicated state before its own frame systems; they stay empty unless the game calls `install_net_server` or `install_net_client` ([Net](Net.md#schedule)). `NetSend` is fixed-only. Game code that needs a one-shot click uses `Schedule::Frame` and `Phase::Game`. Physics uses `fixed_delta_time`. Which systems the engine registers: [Runtime Loop](../architecture/Runtime%20Loop.md).

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

Fatal is also a live entity that lacks `Camera` or `Transform`. Either way that frame draws no world commands. A world with only UI needs no camera. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again. The view matrix is the inverse of the camera entity's world matrix (`ecs::world_matrix`), so a camera with a rotation or a `Parent` works. Each bound window gets one `render::CmdSetView` before its draws, with the same view matrix and camera position (the translation of that world matrix). Its projection is `window_size_for` for that id (`Presentation.sizes`). A `Renderable` or `Sprite` outside the `camera_frustum` of every bound window is not drawn ([Render](Render.md#frustum-culling)). For rendering an arbitrary `World` into a sub-rectangle of a window with an explicit camera (`WorldViewportRender`), call `render_world_viewport` (`include/engine/ecs/systems.h`; see [Render](Render.md#world-draw)).

Light components (`render::DirectionalLight`, `render::AmbientLight`) are plain data on entities; a directional light is aimed by its `Transform::rotation` and follows a `Parent`. Rules: [Render](Render.md#lights).

`view_matrix`, `screen_to_ray`, `screen_to_world` and `world_to_screen` take the camera's world matrix (`const glm::mat4&`, from `ecs::world_matrix(world, camera_entity)`), not its `Transform`. The last three also take the `Camera` and a `WindowSize`. Screen positions are window pixels, origin top-left, +Y down.

| Function | Result |
| --- | --- |
| `screen_to_ray(screen, camera, camera_world, window)` | A `Ray` (`include/engine/ecs/ray.h`: `origin`, unit `direction`) from the near plane away from the camera. Orthographic: every ray is parallel to the camera's forward. Perspective: through the pixel |
| `screen_to_world(screen, camera, camera_world, window)` | Orthographic: the point under the pixel halfway through the clip range (the camera's z for the default -1 and 1). Perspective: where `screen_to_ray` meets the world plane z = 0, which is what 2D-style picking expects; the ray's near-plane origin when the ray is parallel to that plane or the plane is behind the camera |
| `world_to_screen(world_pos, camera, camera_world, window)` | The pixel the point projects to, either projection |
| `camera_frustum(camera, camera_world, window)` | The world-space `render::Frustum` of that view (`include/engine/render/frustum.h`), the one `run_render` culls with ([Render](Render.md#frustum-culling)) |

### Ray queries

`include/engine/ecs/ray.h`, `src/ecs/ray.cpp`. Pure geometry: no world and no components. `t` is the parameter of `direction` (`point = origin + t * direction`). `screen_to_ray` produces a unit direction, so there `t` is a distance. A zero direction, a parallel ray, or `t < 0` is a miss for every half-line query. The hit normal is unit length and faces back along the ray.

| Function | Hit |
| --- | --- |
| `line_plane(ray, point, normal)` | The infinite line and the plane. `t` may be negative. nullopt when the line is parallel or `normal` is zero |
| `ray_plane(ray, point, normal)` | `line_plane` with `t >= 0` |
| `ray_sphere(ray, center, radius)` | The near surface. An origin inside the sphere reports `t = 0`. `radius < 0` misses |
| `ray_aabb(ray, min, max)` | The slab entry, in the ray's space. An origin inside the box reports `t = 0` and a normal of `-normalize(direction)`. A reversed bound is swapped. An oriented box is this after the ray is multiplied by the inverse world matrix; `t` is unchanged |
| `ray_triangle(ray, a, b, c)` | Either side of the triangle |
| `ray_quad(ray, a, b, c, d)` | Triangles ABC and ACD. Corners go around the boundary. The nearer `t` |
| `ray_segment(ray, start, direction, length, radius)` | The half-ray comes within `radius` of the segment. `direction` is normalized; `length` and `radius` are world units. `t` is the closest point on the normalized ray, not a surface entry |

`screen_to_world` for a perspective camera is `ray_plane` against the world plane z = 0, or the ray origin when that misses. `PointerTarget` hit-testing uses `line_plane` against the plane z of each target (a plane behind the camera still yields a point) and then the local rectangle, so it works under a perspective camera. For an unrotated orthographic camera that is the same point as before. The editor gizmo tests its center with `ray_sphere`, an axis with `ray_segment`, and a rotation ring with `ray_plane` plus a distance band.

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
- `update_world_transforms(world)` recomputes the whole forest every call, parents before children, idempotent. There are no dirty flags: `Transform` is a public struct that code mutates directly. It starts from the roots (entities with `Children` and no `Parent`), writes `WorldTransform` on every entity that has a `Parent`, and removes `WorldTransform` from entities that no longer have one. Deleting a `WorldTransform` and updating again gives the same matrix. Callers: the `Frame` / `Transform` system, `run_particles` at its start (it runs in `Game`, before `Transform`), and `run_physics_2d` and `run_physics_3d` after kinematic integration, after dynamic integration, and after the positional solve. Anything else that needs fresh matrices in between calls it too.

Destroying a parent destroys all its descendants (iterative, safe for deep chains), and destroying a child removes it from its parent's `Children`. `World::destroy_immediate` does this through `Children`, a deliberate small coupling of `world.cpp` to `hierarchy.h`. `destroy` inside a view is still deferred: the parent and its descendants stay alive and valid until the view ends, and then the cascade runs.

A `Parent` whose entity is dead can only come from hand-made data (the cascade prevents it). `update_world_transforms` logs one warning, removes that `Parent`, and the entity is a root from then on. `compute_world_matrix` stops its walk at a dead parent without logging.

Staleness contract: the engine refreshes `WorldTransform` in `Phase::Transform` (Frame) and inside `run_physics_2d`, `run_physics_3d`, and `run_particles`. A `Game` system sees the matrices of the previous frame's `Transform` phase plus whatever `run_particles` refreshed, so it should read `compute_world_matrix` or call `update_world_transforms` itself when it needs the result of an edit made this frame. `run_render` needs no refresh, because `Transform` precedes `Render`. `WorldTransform` is only as fresh as the last `update_world_transforms`. After a local `Transform` edit, `set_parent` or `set_world_position`, `world_matrix` still returns the old value until the next update; use `compute_world_matrix` for an exact answer now. `Parent` must be set through `set_parent`; a `Parent` that its parent's `Children` does not list is not visited by the update. A kinematic child's `RigidBody2D.velocity` is in the parent's space, because that body integrates the local `Transform.position`. A dynamic child's velocity is world XY; `run_physics_2d` writes it with `set_world_position`. The same split holds for `RigidBody3D`: kinematic velocity is parent space, dynamic velocity is world space and is written with `set_world_position`.

### Multiplayer readiness

`Transform` and `Parent` are the first two replicated components (slots 0 and 1 of `ReplicationRegistry`); a game adds its own with `register_replicated`. `WorldTransform` and `Children` are derived and rebuilt on the receiving side: the client applies `Parent` through `set_parent` / `clear_parent`. `Entity` handles (index + generation) are not stable across machines, so a replicated entity carries `engine::net::NetIdentity` (its `NetId`), `NetEntityMap` maps the id back to a local entity, and `Parent.entity` travels as the parent's `NetId` (`ReplicaWriter::write_entity`, `ReplicaReader::read_entity`). See [Net](Net.md#replicated-components).

## Physics

Physics is two solvers. The 2D one is `include/engine/ecs/physics_2d.h`. Its components, event and system carry a `2D` suffix (`RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `PolygonCollider2D`, `CompoundCollider2D`, `CollisionEvent2D`, `run_physics_2d`). The 3D solver adds its own types in `include/engine/ecs/physics_3d.h` (`RigidBody3D`, `BoxCollider3D`, `SphereCollider3D`, `CapsuleCollider3D`, `CollisionEvent3D`, `run_physics_3d`). They are not typedefs. `CollisionPhase` (`Enter`, `Stay`, `Exit`), `BodyType` (`Static`, `Kinematic`, `Dynamic`), and the shared step constants (`kMinDynamicMass`, `kContactSlop`, `kBounceThreshold`, `kPositionIterations`, `kSleepLinear`, `kSleepAngular`, `kSleepSteps`) live in `include/engine/ecs/physics.h`. `Physics2DConfig` stays in the 2D header. `RigidBody2D.angular_velocity` is radians per second about world +Z, default 0. `render::ParticleCollider2D` is the XY-only copy of box and circle colliders that `run_particles` hands to `update_emitter`. That copy stays axis-aligned and does not see `PolygonCollider2D`, `CompoundCollider2D`, or box yaw. `render::ParticleCollider3D` is the copy of non-trigger box, sphere, and capsule colliders, oriented by the world `Transform` quaternion. `ParticleGrid` is an optional world context (`try_ctx`); `collision_grid` sweeps it after the particle job joins. `run_physics_2d` integrates on the caller.

`Physics2DConfig` is a world resource, not a component. `world.ctx<Physics2DConfig>()` default-constructs it on first use (`Resource<T>::value{}`), so gravity starts at zero. Games set `gravity` there. It is not serialized.

`run_physics_2d` is the Fixed / Physics system. `dt` is `Time::fixed_delta_time`.

Before integration, a sleeping dynamic wakes when its linear speed is at least `kSleepLinear` (`0.05`) or `|angular_velocity|` is at least `kSleepAngular` (`0.05`). After the kinematic integrate and `update_world_transforms`, it also wakes when a real overlap (narrowphase, a touch included) exists with an awake dynamic or a kinematic whose linear or angular velocity is not exactly 0. Waking clears `sleeping` and the counter in `ctx<PhysicsSleepState2D>` (`physics_2d_internal.h`). That counter is not a component and is not serialized. `advance_sleep` (`src/ecs/physics_sleep.cpp`) steps it, and steps `ctx<PhysicsSleepState3D>` the same way. 2D passes `|angular_velocity|`; 3D passes `length(angular_velocity)`.

1. Kinematic bodies only: `local.position += velocity * dt`. `angular_velocity` is not integrated. Static bodies and dynamic bodies are skipped. The default type is kinematic, so an old body still tunnels.
2. `update_world_transforms`.
3. Dynamic bodies, outside any view. A dynamic with `sleeping` set is skipped: no gravity, no position write, and no yaw. Awake bodies do `velocity += gravity * gravity_scale * dt`, then `set_world_position` to `world_position + velocity * dt`. Targets are snapshotted first and written parents-before-children by hierarchy depth, so a dynamic parent does not drag a dynamic child. A kinematic or static parent has already moved, so that carry is in the snapshot. After every position write, each dynamic body yaws by `angular_velocity * dt`, still parents-before-children. A zero turn is skipped so an identity quaternion stays identity. A root does `rotation = rotation_z(dtheta) * rotation`. A child writes the same world-Z turn back into local rotation: `normalize(inverse(R_parent) * rotation_z(dtheta) * R_parent * R_local)`. The yaw write touches `Transform.rotation` only.
4. `update_world_transforms`.
5. Collect colliders at their **world position**. A collider is **movable** when the nearest `RigidBody2D` on its entity or above it is Dynamic or Kinematic, and **static** otherwise (no body, or a Static body). Movable colliders are gathered each tick by walking the Dynamic and Kinematic bodies and their descendants (down to the next `RigidBody2D`), once, after the kinematic integrate; the overlap wake uses that list, and the dynamic integrate only refreshes its pose. Static colliders are not gathered each tick: their items, AABBs, and AABB tree are cached in `ctx<PhysicsBroadphase2D>` (`physics_2d_internal.h`, `physics_2d_static.cpp`), so the cost of a tick follows the bodies and the contacts, not the static count. Static-static pairs are never queried. Each part of a `CompoundCollider2D` is its own cache slot and static-tree leaf, keyed by `(entity, shape, part)` (see Compound colliders below). Movable colliders and static triggers go into the per-tick AABB tree in `src/ecs/broadphase.cpp` (rebuilt from the current bounds each step, closed bounds, no inflation; 2D proxies store z in `[-1, 1]`, so z never adds or drops a 2D pair) and each queries the cached static tree; only the statics they meet join the tick's collider list. That list is ordered by shape (boxes, circles, polygons, compound parts), then `Entity`, then part index, and pairs are sorted by that order before publish.
   The static cache is rebuilt the next tick, with nothing for the game to do, when a `BoxCollider2D`, `CircleCollider2D`, `PolygonCollider2D`, or `CompoundCollider2D` is added or removed anywhere or an entity is destroyed (`World::structure_version`), and when an entity enters or leaves the movable set (a `RigidBody2D` added, removed, or retyped; a collider parented under a moving body or cleared from it). **Editing static geometry in place is not noticed**: after changing the `Transform` of a static collider or of any ancestor, its collider fields, or its `Parent`, call `mark_static_colliders_dirty_2d(world)` (`physics_2d.h`) and the next tick sees it. A forgotten call is caught by an audit that re-reads 32 cached statics per tick, round robin (about `N / 32` ticks for `N` statics; a compound part counts as one and also compares the shape count it was read with, so a compound that grew or shrank in place is found too); it rebuilds the cache and logs one warning per entity. Geometry that moves every tick should carry a Kinematic body (it is then movable and never cached). A rebuild re-reads every static collider (about 0.7 ms at 2,000 in RelWithDebInfo) and edits the static AABB tree only where a collider was added, removed, or changed; any collider add or remove, a spawned projectile included, costs one such re-read that tick ([Physics Bench](../features/Physics%20Bench.md)). Replication or a serializer read that overwrites a static's components in place is an in-place edit too. The narrowphase still rejects misses. A shared face is a candidate. A sleeping body still publishes. A collider on a child follows its parent. Scale is not applied to `size`, `radius`, or polygon vertices. The collider angle is `atan2` of the cached world matrix (`world_matrix`) X axis in XY (the same epsilon as `angle_z`), read right after `update_world_transforms`. A circle ignores that angle. A box whose angle is exactly `0` stays on the legacy AABB manifold. Any other box angle, and every polygon, uses the convex clipper in `contact_2d_polygon.cpp`. Polygon vertices are local, CCW, at most `kPolygonCollider2DCapacity` (8), and are not recentered; the world polygon is center plus one yaw. Fewer than 3 vertices, a non-finite component, a zero-length edge, a clockwise turn, or a non-positive area produces no contact. A collinear turn is kept. Two colliders on one entity are not a pair. Publish one event per entity pair per tick: when the two entities touch through several shape pairs (compound parts, or several colliders on one entity), `normal`, `penetration`, `point`, and `is_trigger` come from the deepest shape pair (largest `points[0].penetration`), and equal depth keeps the earlier pair in the collider order. The event goes out in the order of the pair's first shape pair, through the reusable scratch `PhysicsOverlapState::pending`. 3D keeps the first manifold (see 3D physics). Publish Enter, Stay, and Exit from this pre-solve pose. The event stores `points[0]` only: the deeper point, or the midpoint when the two clip depths compare equal (the count collapses to 1). Overlap memory is `ctx<PhysicsOverlapState>` (the ordered pair and whether it was a trigger). A separation the solver creates this step still reports Enter or Stay; Exit is the next step with `count == 0`. Slop is not an event threshold. There is no Z test.
6. Solver candidates use that same tree, then the same filters, then the canonical `(min(Entity), max(Entity), shape, part, item)` sort. Real overlaps are partitioned into independent color sets (`src/ecs/contact_graph_coloring.cpp`, up to 24 colors plus an overflow bucket) so no two contacts in the same color share a writable awake dynamic body. Within each iteration, colors execute sequentially in canonical order while contacts inside each color run concurrently across `JobSystem::parallel_for` without atomics or locks (overflow runs serially at the end of the iteration). Because constraints within a color are mutually independent, results are bit-deterministic regardless of worker thread count. A sleeping dynamic enters with inverse mass 0, inverse inertia 0, and solver velocity 0. The solver does not write that zero velocity back over the component. `solve_position_iteration_2d` (`src/ecs/solver_2d.cpp`) is a pure function: bodies and an impulse accumulator in, corrections out, no `World` and no events. The system applies `set_world_position` and writes dynamic `velocity.xy` and `angular_velocity`. Kinematic and static `angular_velocity` are copied into the solver (so a kinematic point velocity includes spin) and are not written back. Inverse inertia is 0 for both. The accumulator seed is `ctx<PhysicsContactCache2D>` (`physics_2d_internal.h`), keyed per shape pair: `ContactCacheKey2D` is the two entities (lower first), the `ColliderShape2D` of each side, and the part index of each side, so two compound parts (or two colliders) touching the same body warm-start separately. The shape angle is sampled at gather and held fixed for the iterations.
7. Before the iterations, each overlapping pair whose cached normal still agrees with the current manifold (`dot > 0`) copies the two per-point impulses into the accumulator, point i into point i. A flipped normal (`dot <= 0`) drops both. The seed is not applied to velocity. The cache stores two impulses per pair and is replaced after the iterations by pairs that overlapped. An empty candidate list clears it. The cache is not serialized. The iteration recomputes the delta from the current relative velocity and clamps it against the seed. `kPositionIterations` (8) times: refresh transforms, copy world XY into the body cache, recompute `make_contact_2d`. Positional correction stays the mass-only push from `points[0]`: `depth = max(0, penetration - kContactSlop)`, `inv = inv_m_a + inv_m_b` (not `inv_n`). A positive `inv` and `depth` move A by `-n * depth * inv_a / inv` and B by `+n * depth * inv_b / inv`. The tangent is `t = (-n.y, n.x)`. Relative velocity at a point includes spin. Friction is `sqrt(fa * fb)` after each friction is clamped to `>= 0` (negative or non-finite is 0). Restitution is `max(ra, rb)` after each restitution is clamped to `[0, 1]` (non-finite is 0). The component values are not written back. If `v_rel_n >= -kBounceThreshold` (`1`), restitution is 0 for that impulse, so a resting body under gravity `(0, -60)` approaches at exactly 1 and does not bounce. If `inv == 0` the velocity step is skipped. Each contact point then takes its own impulse. `r` is `point - body position` captured before the positional push. `v_point = v_linear + omega * (-r.y, r.x)`. `rn = r.x * n.y - r.y * n.x` and `rt = r.x * t.y - r.y * t.x`. `inv_n = inv_m_a + inv_m_b + rn_a^2 * inv_I_a + rn_b^2 * inv_I_b`, and `inv_t` is the same with `rt`. `j_n = -(1 + e) * v_rel_n / inv_n` when `v_rel_n < 0`, else 0, accumulated per point and clamped at 0. `j_t = -v_rel_t / inv_t`, then that point's tangent accumulator is clamped to `[-mu * that point's accumulated normal, +mu * it]`. Both points are full weight: there is no one-half factor. `omega_a -= lever * j * inv_I_a` and `omega_b += lever * j * inv_I_b` for the normal (`rn`) and the tangent (`rt`). Only a dynamic body is written. Static inverse mass is 0 and its velocity is treated as 0. Kinematic inverse mass is 0 and its velocity is not written. A root kinematic's `velocity.xy` is world XY. A kinematic with a parent contributes `world_matrix(parent) * vec4(velocity, 0)` XY, which is the parent's linear map (rotation and scale included), matching `T * R * S`. The kinematic's own rotation is not applied. Writes go out parents-before-children by depth. After the iterations the cache is replaced by the pairs that overlapped in at least one iteration (final accumulator and that hit's normal). Pairs that did not contact, and a step with no candidate pairs, drop their entries. Because the cache is not added to velocity again, a resting contact's normal accumulator grows by about the new `j_n` each step and the friction cone widens with it. Coulomb friction therefore removes about `mu * |g| * dt` of tangent speed per resting step.
8. `update_world_transforms` once more so later systems see the solved pose.

After the velocity write, a dynamic whose linear speed is below `kSleepLinear` and whose `|angular_velocity|` is below `kSleepAngular`, and that has at least one solver manifold, increments the ctx counter (clamped at `kSleepSteps`, 8). Reaching 8 sets `sleeping`. Otherwise the counter is cleared, and `sleeping` is cleared when either speed is at or above the threshold. No solver contact does not start sleep, and a sleeper whose contact disappears stays `sleeping`. `RigidBody2D.sleeping` is the last field and is serialized. A missing key loads false. Kinematic and static bodies ignore the flag.

Inverse mass of a dynamic body is `1 / m` with `m = mass > kMinDynamicMass ? mass : kMinDynamicMass`. Inverse inertia is `1 / I` for a dynamic body and 0 otherwise. `I` is computed once, from the first non-trigger collider in gather order (boxes, then circles, then polygons), even when that `I` is 0. Extra colliders still generate contacts. They do not contribute another full mass. A `CompoundCollider2D` never reaches this: under a Dynamic body it is left out. A box uses `m * (width^2 + height^2) / 12` with the full size. A circle uses `0.5 * m * radius^2`. A polygon uses `m * sum(D * (x0^2 + x0*x1 + x1^2 + y0^2 + y0*y1 + y1^2)) / (6 * sum(D))` with `D = x0*y1 - y0*x1` over the edges, about the vertex-list origin (an off-center polygon is not recentered). `make_contact_2d` (`src/ecs/contact_2d.cpp`) keeps the legacy one-point manifold when both boxes have angle exactly 0, when both shapes are circles, or when an angle-0 box meets a circle: X wins a tie, a penetration of 0 is a touch, and a non-finite or negative size returns an empty manifold. Do not give a predicted or controlled entity a `RigidBody2D` ([Net](Net.md)). `run_physics_2d` is outside prediction `step`, so a kinematic body still integrates and a dynamic body is solved there. An entity with `RigidBody3D` is skipped by the 2D integrator and by 2D pair tests (events and the solver) through `claimed_by_physics_3d` (`src/ecs/physics_owner.cpp`), so it is not moved twice. Do not put both rigid bodies on one entity.

### Compound colliders

`CompoundCollider2D` (`physics_2d.h`) puts many convex shapes on one entity, so chunked terrain (one 32x32 tilemap chunk, one marching-squares chunk) is one entity instead of one per cell. Every ECS pass that walks colliders or the hierarchy then pays per chunk, not per cell ([Physics Bench](../features/Physics%20Bench.md)).

- `shapes` is a `std::vector<CompoundShape2D>`; each shape is a convex polygon of at most `kPolygonCollider2DCapacity` (8) vertices, CCW, in the entity's local XY about its origin, not recentered and not scaled, with the validity rules of `PolygonCollider2D`. `box_shape_2d(center, size, angle)` builds a box as a 4-vertex shape. Boxes are polygons here, so an angle-0 compound box uses the convex clipper, not the legacy AABB manifold. There is no cap on the shape count.
- One material per component: `layer`, `mask`, `friction`, `restitution` apply to every shape. No `is_trigger`: static triggers are queried every tick, so trigger regions stay separate entities.
- Where it is supported: static (no body, or a `Static` body), and under a `Kinematic` body, where the parts are movable colliders gathered every tick like any other. Under a `Dynamic` body (on the entity or above it) the compound is left out entirely, not cached as static either, and `log::warn` fires once per entity. Dynamic compounds (mass, inertia, centroid) are future work.
- Internally a part is a `ColliderItem2D` with `ColliderShape2D::Compound` and its index in `part`; every shape function treats it as a polygon at the entity's pose. Static parts expand into one cache slot and one static-tree leaf each, owned by one entity (`static_entities` lists it once).
- Adding or removing the component is noticed through `structure_version<CompoundCollider2D>()`. Editing `shapes` (or `layer`, ...) in place is an in-place static edit: call `mark_static_colliders_dirty_2d`. The rebuild re-reads every static part, so the cost of a chunk edit follows the total static part count, as for single colliders.
- Events and contact cache: see steps 5 and 7 above. A dynamic body sliding across the parts of one compound stays one Stay stream, with no Exit or Enter per part.
- Grid queries and ray queries are pure math and do not read colliders, so they need no compound support. `run_particles` does not see compound shapes.

### 3D physics

`run_physics_3d` is registered once, in `register_simulation_systems`, on `Schedule::Fixed` / `Phase::Physics`, immediately after `run_physics_2d`. Games must not register it again. `dt` is `Time::fixed_delta_time`. `Physics3DConfig` is a world resource, default gravity zero, not serialized. The default `BodyType` is kinematic.

The pipeline matches 2D, with a world quaternion instead of a Z yaw:

1. Kinematic bodies only: `local.position += velocity * dt`. No angular integration for kinematic or static.
2. `update_world_transforms`.
3. Dynamic bodies, snapshotted, parents first. `velocity += gravity * gravity_scale * dt`, then `set_world_position(world_position + velocity * dt)`. Then, still parents first, `rotation = quat(ω dt) * rotation` for a root, and `normalize(inverse(R_parent) * quat(ω dt) * R_parent * R_local)` for a child. `quat(ω dt)` is `angleAxis(|ω| dt, ω/|ω|)`. An `angular_velocity` of exactly 0 is skipped so identity stays identity.
4. `update_world_transforms`.
5. Gather `BoxCollider3D`, then `SphereCollider3D`, then `CapsuleCollider3D` at the world position and the quaternion chain. Scale is not applied. Publish `CollisionEvent3D` from this pre-solve pose. Overlap memory is `ctx<PhysicsOverlapState3D>`. Exit zeros the normal, the penetration, and the point. A pair of colliders on the same entity is skipped, and a second shape pair of the same two entities keeps the first manifold.
6. Non-trigger pairs with at least one dynamic body, sorted canonically by `(min(Entity), max(Entity), shape, item)`. Real overlaps are partitioned into connected dynamic islands (`build_contact_islands` in `src/ecs/contact_islands.cpp`, where static and kinematic bodies do not bridge separate dynamic islands), ordered by lowest entity id. The 3D solver uses a hybrid two-level parallel strategy: independent small and medium islands (`<= 100` dynamic bodies) execute in parallel across `JobSystem::parallel_for` (one island per task), whereas large islands (`> 100` dynamic bodies) build a `ContactColorGraph` (`src/ecs/contact_graph_coloring.cpp`) and solve each color in parallel across worker threads per iteration. `solve_position_iteration_3d` (`src/ecs/solver_3d.cpp`) takes no `World`. The system writes dynamic `velocity`, `angular_velocity`, and `set_world_position`. Kinematic and static angular velocities are copied in for the point velocity and are not written back. The shape rotation is sampled at gather and held fixed for the iterations. The solver does not rotate the body while iterating.
7. `ctx<PhysicsContactCache3D>` seeds one normal impulse and two tangent impulses when `dot(cached_normal, new_normal) > 0`. The seed is not applied to velocity. The cache is replaced by pairs that overlapped and is not serialized.
8. `update_world_transforms` once more.

Positional correction is the mass-only push of the one contact point: `depth = max(0, penetration - kContactSlop)`, A moves `-n * depth * inv_a / inv`, B moves `+n * depth * inv_b / inv`. Two tangent axes: `t1 = normalize(n × +Y)` when `|n.y| < 0.9`, otherwise `normalize(n × +X)`, and `t2 = n × t1`. Each tangent is clamped on its own to `±μ j_n` (a pyramid). Friction is `sqrt(fa * fb)`, restitution is `max(ra, rb)`, and restitution is 0 when `v_rel_n >= -kBounceThreshold`. `v_point = v + ω × r`. `inv_d = inv_m_a + inv_m_b + dot(ra × d, invI_a (ra × d)) + dot(rb × d, invI_b (rb × d))`. `j_n = -(1+e) v_rel_n / inv_n` when approaching. `ω += invI (r × j)` for a dynamic body only.

`make_contact_3d` (`src/ecs/contact_3d.cpp`, `contact_3d_box.cpp`, `contact_3d_capsule_box.cpp`) does not call `make_contact_2d`. Sphere-sphere, sphere-capsule, and capsule-capsule use the closest points of the segments (a capsule segment is `center ± world_Y * height/2`; parallel overlap uses the overlap midpoint; coincident centers use `+X`). Sphere-box is the closest point on the OBB, and an identity box matches the AABB. Identity box-box is the overlap-box center (X, then Y, then Z). Any other box-box is SAT with one witness. Capsule-box is the closest point of the segment to the OBB, or the deepest interior point when the segment hits the box. A negative radius, height, or size returns an empty manifold. A penetration of 0 is a touch. v1 stores one point.

Inverse inertia is a world tensor `R diag(1/I) R^T`, zero on any axis whose `I` is not positive. Local box inertia is `m/12 * (sy²+sz², sz²+sx², sx²+sy²)` with the full size. A solid sphere is `0.4 m r²` on every axis. A capsule is a uniform-density cylinder of length `height` plus two hemispheres: `V_cyl = π r² h`, `V_sph = 4/3 π r³`, masses split by those volumes, `I_axis = 1/2 m_cyl r² + 2/5 m_sph r²`, and the other two axes are the cylinder's `m_cyl (3 r² + h²) / 12` plus each hemisphere's `2/5 m_h r²` shifted by `h/2`. `height == 0` matches the sphere. The first non-trigger collider in gather order supplies `I`. Later solid colliders still contact and do not add mass. A root kinematic's velocity is already world space. A kinematic child contributes `world_matrix(parent) * vec4(velocity, 0)`, scale included; its own rotation is not applied to that velocity. Collider size still ignores scale. Sleep and the broadphase match the 2D pipeline. `kSleepLinear` and `kSleepAngular` are `0.05` (angular speed is `length(angular_velocity)`) and `kSleepSteps` is 8. The quiet counter is `ctx<PhysicsSleepState3D>`. `RigidBody3D.sleeping` is serialized and a missing key loads false. Do not give a predicted or controlled entity a `RigidBody3D`. `run_physics_3d` runs outside prediction `step`, so a kinematic body still integrates and a dynamic body is solved. Characters and tiles stay on `sweep_aabb`.

### Grid queries

`include/engine/ecs/grid_query.h`, `src/ecs/grid_query.cpp`. Collision against a grid of solid cells and a ray through cells, for block worlds and tile games. Pure math like [Ray queries](#ray-queries): no world, no components, nothing registered. It needs no rigid-body 3D physics.

Grid space: cell `(i, j, k)` covers `[i, i+1) x [j, j+1) x [k, k+1)`. The cell size is 1 and the origin is 0; there is no cell-size parameter, so the caller scales world coordinates into grid space (a ray's `t` survives the scaling when its direction is scaled too). +Y is up. Precision: keep coordinates within about +-8192 cells, where a float step is still finer than `kGridSkin`; a position snapped to a face is an exact integer at any range.

`is_solid` is a `CellPredicate<glm::ivec3>` (2D: `glm::ivec2`): a non-owning reference to a `bool(Cell)` callable, two pointers and one indirect call per cell, like C++26 `std::function_ref`. It is valid only for the call it is passed to; never store one. Pass a lambda at the call site; a free function goes through a lambda or a function-pointer variable. The algorithms stay in the `.cpp` (one template over the dimension), so a game does not compile them.

| Function | Result |
| --- | --- |
| `raycast_grid(ray, max_distance, is_solid)` | `std::optional<GridHit>`: the first solid `cell`, the face entered (`normal`, one axis +-1, facing back along the ray; `cell + normal` is the cell the ray came from), `t` and `point` |
| `raycast_grid(origin, direction, max_distance, is_solid)` | 2D over `glm::vec2`: `GridHit2D` |
| `sweep_aabb(box, delta, is_solid)` | `GridSweep`: the resolved `render::Aabb`, `moved` (the applied displacement), `hit` per axis (0 free, +1 blocked moving +axis, -1 blocked moving -axis), `grounded()` = `hit.y < 0` |
| `sweep_aabb(min, max, delta, is_solid)` | 2D: `GridSweep2D` |

Ray (Amanatides-Woo DDA):

- `t` and `max_distance` are multiples of `direction`, as in `ray.h`: distances for a unit direction. `t <= max_distance` hits.
- A boundary is half-open in the direction of travel: a ray that starts on a face and points away does not touch the cell behind it.
- A ray that starts inside a solid cell reports `t = 0`, a zero normal and the origin, even with a zero direction or `max_distance = 0`.
- A ray exactly through an edge or a corner (boundary `t` equal within 1e-6, relative) is blocked by any solid cell that touches it there. The side cells are tested x, y, z, then xy, xz, yz, then the diagonal cell; the normal is the first stepped axis of that cell. A line of sight cannot slip through the seam between two diagonal blocks.
- nullopt: a miss within `max_distance`, a zero direction outside a solid cell, `max_distance < 0`, or a non-finite input (or a coordinate of 2^24 or more, where a float no longer tells one cell from the next).

Sweep:

- Axis-separated: Y, then X, then Z (2D: Y, X). Each axis stops flush against the first solid face ahead of the box. Every face crossed along the axis is tested, so no speed tunnels through a wall.
- A move is cut into `ceil(max |delta|)` sub-steps, so a fast diagonal move cuts no corner larger than a cell. A box or delta of 2^24 or more returns the box unmoved. An axis blocked in one sub-step stays blocked; the others keep sliding.
- `kGridSkin` (1e-3 grid units): the cells across the motion come from the box shrunk by the skin, so a box resting on a floor or flush against a wall slides along it and does not catch on cell seams. A box sunk up to the skin into a face it moves toward snaps back onto it. A box resting on a floor under a downward delta snaps to exactly the same face every call: grounded, no drift, no jitter.
- A move that ends exactly on a face counts as blocked. A zero delta on an axis reports no hit there.
- Cells the box already overlaps deeper than the skin are ignored: a box inside a solid is never stuck, and is not pushed out.
- No step-up: a one-cell step blocks horizontal motion. A reversed bound is swapped. A non-finite box or delta returns the box unmoved.

The engine has no tile collision flag; a tile layer is a lambda. Tilemap rows run down while y runs up, so grid cell `(x, y)` is row `-y - 1`: `map.tile(layer, c.x, -c.y - 1) != 0`, with the box taken relative to the map's position and divided by `TilemapAsset::cell_size()`.

## World serialization

`include/engine/ecs/world_io.h`, `include/engine/ecs/world_asset.h`, `include/engine/ecs/world_serializer.h`.

`.world` TOML files (`importer = "world"`) parse into a `WorldAsset` (`version = 1`, optional `active_camera`, and `[[entities]]` with `id`, optional `parent`, and `[entities.<component>]` tables).

- `WorldSerializer` (available via `world.ctx<WorldSerializer>()`) comes pre-populated with built-in engine components: `transform`, `camera`, `sprite`, `sprite_animator`, `renderable`, `tilemap`, `directional_light`, `ambient_light`, `point_light`, `spot_light`, `fog`, `rigid_body_2d`, `box_collider_2d`, `polygon_collider_2d`, `compound_collider_2d`, `circle_collider_2d`, and `pointer_target`. `rigid_body_2d` writes `angular_velocity`; a missing key stays 0. `polygon_collider_2d.vertices` is an array of `[x, y]` of length `count` (more than 8 fails the load). `compound_collider_2d.shapes` is an array of tables `{ vertices = [[x, y], ...] }` with the same 8-vertex limit, plus `layer`, `mask`, `friction`, `restitution`; a prototype variant that overrides `shapes` replaces the whole array. Each entry's `remove` drops that component; `register_component` installs it. Removing `sprite`, `sprite_animator`, `renderable`, or `tilemap` also clears the asset ids saved for that component.
- Game-specific components marked `struct WIND_COMPONENT MyComponent { ... };` are registered by calling `assets::register_game_components(world.ctx<WorldSerializer>())` from the generated `<component_io.h>` header (see [Asset Codegen](../build/Asset%20Codegen.md#component-codegen)), or by calling `GameModule::register_components(serializer)` (`wind_register_game_components`).
- `struct WIND_SERIALIZABLE Value { ... };` is not a component. The same header emits `encode_wind` / `decode_wind`, so a component field of that type (or a `std::vector` of it) is nested in the component's table in the `.world` file and is not its own asset. `AssetId` on a component stays a guid; the game loads the referenced `.object` with `AssetsDb::get<T>`. `assets::register_game_objects` registers those loaders (`GameModule::register_objects`, `wind_register_game_objects`).
- `instantiate_world(world, asset, &assets, serializer, unknown_mode)` and `load_world_from_toml` spawn entities in two passes (entities and `EntityName` first so `ecs::Entity` cross-references resolve in any order, then hierarchy and components), set `ActiveCamera` when `active_camera` is non-empty, and call `update_world_transforms(world)`. On any error, newly created entities are rolled back.
- `UnknownComponentMode::Reject` (default) fails with `WorldErrorKind::UnknownComponent` when a component key is not registered in the active `WorldSerializer`. `UnknownComponentMode::Preserve` stores unregistered component tables in `UnknownComponents` (`std::map<std::string, FieldTable, std::less<>> components`) on the entity so the editor can inspect and round-trip game components without losing data when the game module is not loaded.
- `capture_world_asset` and `save_world_to_toml` snapshot all alive entities (`World::alive_entities()`), their registered components, and any preserved `UnknownComponents` entries back to a `WorldAsset` or TOML string.

## Prototypes

`include/engine/ecs/prototype_asset.h`, `src/ecs/prototype_asset.cpp`.

`.prototype` TOML files (`importer = "prototype"`) define reusable entity templates and hierarchies, optionally inheriting from a `base` prototype GUID (Prototype Variants):

- `PrototypeAsset` contains `version = 1`, optional `base` (`AssetId`), and `[[entities]]` records.
- `resolve_prototype_asset(asset, &assets)` resolves the variant inheritance chain, detecting cyclic references (`PrototypeCycle`), deep-merging component `FieldTable`s (via `merge_field_tables`), preserving base children, and adding new entities or components.
- In `.world` files, entities reference prototypes via `prototype = "<AssetId>"`. During `instantiate_world`, prototype hierarchies are automatically expanded, world-level entity overrides (including child entity overrides via `id = "<entity_id>/<child_id>"`) are deep-merged, and `PrototypeInstance{ prototype, entity_sub_id }` is attached to every spawned entity.
- Programmatic spawning in C++: `spawn_prototype(world, prototype_id, assets, params)` spawns a prototype into an active world with optional position, rotation, scale, and parent entity.

## Models

`include/engine/ecs/model.h`, `src/ecs/model.cpp`. The cooked asset is [Assets](../features/Assets.md#models); the draw is [Render](../modules/Render.md#skinning).

A `.world` entity with a `model` component (`ModelPlacement`: asset id and clip name) is expanded by `instantiate_world` after its other components are read, the same way a prototype is. `spawn_model(world, id, assets, params)` does the same for a new root: position, rotation, scale, and parent, matching `spawn_prototype`.

The root gets `Transform` and `ModelAnimator`. Each primitive is a child (`Parent`) tagged `ModelPartTag`. The first primitive of a node drives that node's transform; further primitives of the node parent to it. `capture_world_asset` skips `ModelPartTag`, so saving the world does not write the generated hierarchy back. Model meshes are not catalog entries.

`run_model_animations` is `Schedule::Frame`, `Phase::Game`, and advances by `Time::delta_time` (presentation, like sprite frames). It writes each driving part's local `Transform` from the glTF global of its node, decomposed to TRS, before `update_world_transforms`. `play(clip, fade_seconds)` is layer 0. `play_layer(index, clip, fade_seconds)` starts that layer, creating slots up to `index`. Each layer has its own clock, loop, speed, weight, and crossfade. A fade of 0 switches immediately; otherwise the previous pose of that layer blends out with a linear blend and a slerp of rotation. `ModelBlend::Override` mixes the layer onto the pose under it. `ModelBlend::Additive` adds the clip's delta from the rest pose. `mask` empty is the whole body; a name is that node and its descendants. A name that matches no node leaves the pose unchanged and sends no markers; the clock still advances. A later layer wins where masks overlap. Weight 0 still advances the clock and sends no markers. Markers authored on the model's `.meta` ([Assets](../features/Assets.md#models)) ride every layer whose weight is above 0 and whose mask names a real node. A tick sends one `ModelAnimEvent` (`entity`, `clip`, `name`) for each marker in `(previous, next]`, and the tick after `play` or `play_layer` also sends markers at time 0. The clip `spawn_model` and world expansion start does the same on its first tick. A loop fires each crossing, including the wrap through 0. A paused layer or a `speed` of 0 sends nothing. The clip being faded out sends nothing, so cancelling an attack does not fire the swing it left. The event is not damage: a game system on this same frame latches a component, and the fixed step reads that component. `flush_events` runs at the start of the next tick, before `Schedule::Fixed`, so a fixed system that reads `ModelAnimEvent` directly can miss it on a frame that takes no fixed step. After the layers, each `ModelJointSpring` integrates on the same frame delta and adds its offset to that node's local pose; children follow through the hierarchy. `impulse_joint(animator, name, world_direction, strength, root_world)` adds velocity on that node (a second call in the same tick sums). The direction is world space, mapped by `root_world` (identity when the root is unrotated). The spring settles back to the clip pose. `joint_world(world, root, name)` is that node's global matrix times the root's world matrix, without the inverse bind, for attaching something to a bone. `nearest_joint(world, root, point)` is the node whose posed world position is closest to `point`. The engine does not collide the shot. There is no entity per bone.

## Public headers

- `include/engine/ecs/world.h`
- `include/engine/ecs/world.inl`
- `include/engine/ecs/world_io.h`
- `include/engine/ecs/world_asset.h`
- `include/engine/ecs/world_serializer.h`
- `include/engine/ecs/prototype_asset.h`
- `include/engine/ecs/model.h`
- `include/engine/ecs/entity.h`
- `include/engine/ecs/schedule.h`
- `include/engine/ecs/events.h`
- `include/engine/ecs/transform.h`
- `include/engine/ecs/hierarchy.h`
- `include/engine/ecs/camera.h`
- `include/engine/ecs/pointer_target.h`
- `include/engine/ecs/physics.h`
- `include/engine/ecs/physics_2d.h`
- `include/engine/ecs/grid_query.h`
- `include/engine/ecs/ray.h`
- `include/engine/ecs/systems.h`

## Tests

`tests/ecs_test.cpp`, `tests/hierarchy_test.cpp`, `tests/hierarchy_systems_test.cpp`, `tests/events_test.cpp`, `tests/camera_test.cpp`, `tests/physics_2d_test.cpp`, `tests/physics_2d_static_cache_test.cpp`, `tests/physics_2d_compound_test.cpp`, `tests/grid_query_test.cpp`, `tests/ray_test.cpp`, `tests/pointer_target_test.cpp`, `tests/world_serializer_test.cpp`, `tests/prototype_test.cpp`.

## See also

- [Principles](../architecture/Principles.md)
- [Core](Core.md)
- [Materials and Sort](../features/Materials%20and%20Sort.md)
