# Particle plan

This is a plan. It is not a description of the engine as it runs today. The running emitter is [Render](../modules/Render.md#sprites-animation-particles-curves). Task numbers continue after `wind-326`; renumber if another task takes a number first.

## Goal

One `ParticleEmitter` covers a 2D card, a camera-facing card in a 3D scene, and a volumetric instance (a cube, a block fragment). Simulation already moves a `vec3`. `Plane` still draws in world XY. `Billboard` and `Stretched` face the camera and depth-sort when their material depth-tests. Mesh instances are in. 3D collision is in. Emitter culling is in (wind-332).

An emitter that leaves the new fields at their defaults still expands a quad in world XY, still skips the depth test, and still collides only with `ParticleCollider2D`. An on-screen 2D emitter still draws. One whose quad misses every window frustum does not, unless `frustum_cull` is false.

## As built today

- `Particle` stores `position` and `velocity` as `vec3`. Gravity is a `vec3`. `EmitterShape::Box` fills `shape_scale` on all three axes. `Circle` writes `z = 0`. `Sphere` fills the ellipsoid of `shape_scale`. `Disc` samples the plane of `direction`, and `(0, 1, 0)` when that vector is near zero.
- The builtin particle shader (OpenGL `kParticleVert`, Vulkan `kParticleSlang`) takes `uParticleMode`. `Plane` (0, the default) still builds the quad in world XY: `instancePos + vec3(rotated.xy, 0)`. `Billboard` and `Stretched` add `particle_card_offset` (camera right and up are the first two rows of `uView`). `Stretched` reads instance velocity. `Billboard` and `Stretched` use the material's depth test, depth write, and cull, and depth-sort when that material depth-tests. `Mesh` does not use that shader. 3D collision is in. Emitter culling is in: `collect_world_draws` skips an emitter no bound frustum hits. `frustum_cull` defaults to true and is not serialized.
- Card draws ignore `CmdDrawParticles.mesh` and draw a unit quad. `run_render` fills `mesh` from `emitter.mesh`, or `builtin::mesh_quad` when a card's mesh is null. `Mesh` does not substitute the quad: a null mesh draws nothing, and the backends instance `emitter.mesh` in one draw with `uModel` identity and `uInstanced` 1.
- `Plane` still forces depth test off, depth write off, and no cull. `Billboard` and `Stretched` copy the material's depth test, depth write, and cull. Blend stays the command's blend. A card depth-sorts when the mode is not `Plane` and the material has `depth_test`. A transparent depth-sorting command sorts its instances back to front and takes its sort key from the farthest particle. `Plane`, and a depth-off card, keep survival order and the key at `model[3]` (the world origin in world space, the emitter translation in local space). An opaque depth-writing card joins the opaque queue and is not instance-sorted.
- `Particle::size` is `vec2`. `rotation` and `angular_velocity` are one float about Z.
- `collision_enabled` copies non-trigger `BoxCollider2D` and `CircleCollider2D` into `ParticleCollider2D`. The box test uses X and Y only, so the wall is infinite in Z. Polygons are not copied. Box yaw is not copied. Non-trigger `BoxCollider3D`, `SphereCollider3D`, and `CapsuleCollider3D` are copied into `ParticleCollider3D`. A box is oriented by the world `Transform` quaternion. Triggers stay out. `collision_grid` plus a `ParticleGrid` context sweeps solid cells after the integrate job joins. A missing context does nothing.
- Particles are culled from the live particles and the mode's extent (`frustum_cull`, default true, not a `.world` key). `Plane` uses half `|size|` on XY and 0 on Z, so a rotated quad can extend outside that box. `Billboard` and `Stretched` use a radius of half the longer side, which does not cover the corner. `Mesh` uses the scaled mesh bounds, including an off-center box and a negative scale. `Plane`, `Billboard`, and `Stretched` are not lit and not fogged. A custom card shader is used only when it does not declare `uModel`. `Mesh` uses the material shader, so builtin lit and unlit (and their array variants) run, including fog and `alpha_cutoff`, when `uInstanced` is 1. A shader that does not declare `uInstanced` and instance locations 7, 8, and 9 skips that emitter and is logged once per process.
- `CmdDrawParticles` is one draw. `Mesh` is one instanced draw of `emitter.mesh`. `mesh_scale` times `size_curve` (or 1 when that curve is empty) scales it. `size` and `size_curve_xy` do not. Orientation is identity.

## What this is not

- Not a GPU simulation. `update_emitter` stays on the CPU. `JobSystem` still integrates ranges of 64 and does not call engine APIs.
- Not a second emitter component. The draw mode is a field on `ParticleEmitter`.
- Not a general instancing API. Dropped items and other repeated `Renderable`s stay in the [Scope](Scope.md) backlog. This plan instances one mesh from one `CmdDrawParticles`.
- Not soft particles. Fading against the scene depth waits on a sampled depth buffer ([Post Process Plan](Post%20Process%20Plan.md)).
- Not a lit billboard. `Plane`, `Billboard`, and `Stretched` stay on the particle shader, unlit, no fog. A cube that should take the forward lights uses `Mesh` and the mesh shader.
- Not a replacement of `Particle::size`. Cards keep `vec2`. A cube's base size is a separate `mesh_scale`. The uniform `size_curve` multiplies that scale. `size_curve_xy` applies only to cards.

## Shape

`ParticleDraw` on the emitter. Default `Plane`.

| Mode | Quad or mesh | Depth |
| --- | --- | --- |
| `Plane` | World XY, the shader as it is today | Forced off, as today |
| `Billboard` | Camera right and up | The material's `RenderState` |
| `Stretched` | Along velocity, the other axis in the camera plane. Near-zero speed falls back to `Billboard` | The material's `RenderState` |
| `Mesh` | `emitter.mesh` (a cube when the game sets one), one instance per particle | The material's `RenderState` |

`Plane` is the 2D path. The other three are opt-in. `Mesh` instances `emitter.mesh` in one draw, with `uModel` left as identity.

Card expansion is a pure function the shader repeats: given the camera right and up (the first two rows of `uView`, normalized), the corner, `size`, and `rotation`, it returns the world offset. Tests lock that function. The shader is not screenshot-tested ([Boundaries](Boundaries.md)).

`Stretched` needs the particle's velocity in the instance. `Billboard` does not.

`Mesh` does not go through the particle shader. Builtin `shader_lit`, `shader_unlit`, and their array variants learn an instance transform: when `uInstanced` is 1, the vertex stage uses the instance position, `mesh_scale * size_curve`, and vertex color, and `uModel` stays identity. When `uInstanced` is 0, those shaders are unchanged. A material whose shader does not declare the instance inputs is skipped for that emitter and logged once per process. Texture-array layer is an emitter field copied onto the particle at spawn (default 0). A `sampler2D` shader ignores it.

One `CmdDrawParticles` stays one draw. Transparent cards and transparent cubes sort their instances back to front before the draw. They do not interleave with other transparent draws particle by particle: the command's sort key uses the emitter's farthest particle when the mode depth-sorts. Opaque cubes (`Opaque` blend and depth write) go in the opaque queue and let the depth buffer order them against the world, so they are not instance-sorted.

Bounds for culling and for that key are the positions grown by the card extent (half the longer side of `size` for a billboard, half the XY size for `Plane`) or by the scaled mesh bounds for `Mesh`.

## Where the code lives

Draw mode, `mesh_scale`, and `texture_layer` live on `ParticleEmitter` (`include/engine/render/particles.h`). The card-offset function lives next to the simulator in `src/render/particles.cpp` and is declared for tests. `run_render` (`src/ecs/systems.cpp`) picks the sort key, sorts instances, and still pushes one command. Both backends branch on the mode: the quad path for the three card modes, the mesh path for `Mesh`. The 2D collider copy stays. A separate `ParticleCollider3D` is filled beside it. `update_emitter` takes both spans. The 2D box test is not extended to Z.

Grid collision is not a copy of every solid cell. `update_emitter` takes a `ParticleGrid` pointer (a function pointer plus a `const void*` the game owns) and, when `collision_grid` is set and `is_solid` is non-null, sweeps a box of `collision_radius` (a minimum of one cell when the radius is 0 and the mode is `Mesh`, so a cube cannot tunnel through a corner). `run_particles` passes it only when the emitter sets `collision_grid` and `try_ctx<ParticleGrid>()` is set. The `CellPredicate` is built at the sweep call and is not stored across frames. It runs on the caller after the integrate job joins, so a job still does not call engine APIs and does not call game code. The pointer's data must stay alive through `run_particles`. No context, or a null `is_solid`, means the flag does nothing.

`EmitterShape::Circle` stays an XY disc. `EmitterShape::Sphere` fills the ellipsoid of `shape_scale`. `EmitterShape::Disc` is a disc whose normal is `direction`.

## Slices

Each slice ships on its own. After every slice, an emitter that did not set the new fields draws and collides as it does today.

| Phase | Tasks |
| --- | --- |
| 1. Cards in a 3D scene | wind-327 billboards, wind-328 depth and sort |
| 2. Volumes | wind-329 mesh instances |
| 3. World | wind-330 3D and grid collision, wind-331 sphere and disc |
| 4. Cull | wind-332 emitter frustum |

wind-330 and wind-331 do not depend on the draw slices. wind-328 depends on wind-327. wind-332 depends on wind-327, and on wind-329 for mesh bounds.

### wind-327 feat: camera-facing particles

Do:

- Add `ParticleDraw` with `Plane` (default), `Billboard`, and `Stretched`. No `Mesh` yet.
- Card-offset function and the same math in the OpenGL and Vulkan particle shaders. `Stretched` carries velocity on the instance.
- `particle_test` covers a facing quad, a 90° spin in that plane, stretch along velocity, and the zero-speed fallback. `Plane` offsets stay in XY.

Done when: a default emitter's instance layout and shader for `Plane` match today's quad. Both backends compile the new shader.

### wind-328 feat: depth and instance sort

Do:

- `Billboard` and `Stretched` use the material's depth test, depth write, and cull. `Plane` still forces them off.
- When the mode depth-sorts, sort that command's instances back to front and set the sort key from the farthest particle, with `depth_tested` true. `Plane` keeps `depth_tested` false.
- The default material `run_render` builds for a texture stays depth-off. A 3D card needs a material that sets `depth_test`.

Done when: `render_system_test` shows a `Plane` emitter still unordered and not depth-sorted, and a `Billboard` emitter with depth test reorders instances and takes a depth-sorted key. Opaque-queue membership still follows `draw_queue`.

### wind-329 feat: mesh particles

Do:

- Add `ParticleDraw::Mesh`, `mesh_scale` (`vec3`, default 1), and `texture_layer` (default 0).
- Instance the emitter mesh from `CmdDrawParticles`. `uInstanced` on the builtin lit, unlit, and array vertex shaders. A shader without the instance inputs skips the emitter and logs once.
- `size_curve` scales the cube. `size` and `size_curve_xy` do not. No per-particle tumble in this slice: orientation is identity. Spin stays the card's scalar `angular_velocity`.
- Opaque mesh particles take the opaque queue. Transparent ones sort instances like wind-328.

Done when: a test builds the instance transform (position, scale, layer, color) without a GPU. A `Plane` emitter never sets `uInstanced`. Document the mode in [Render](../modules/Render.md).

### wind-330 feat: 3D particle collision

Do:

- `ParticleCollider3D` for box, sphere, and capsule, copied in `run_particles` from the non-trigger 3D colliders the way `ParticleCollider2D` is copied. A box is oriented: the segment is tested in the box's local space. Triggers stay out. Layer mask is the same `collision_mask`.
- `update_emitter` takes the 3D span beside the 2D span. Bounce, friction, `lifetime_loss`, and `kill_on_collision` are shared.
- `collision_grid` plus `world.ctx<ParticleGrid>()` run `sweep_aabb` on the caller after the job joins. No context means the flag does nothing.

Done when: `particle_test` covers a 3D box hit on Z (the 2D box still ignores Z), a sphere, a capsule, and a grid sweep that stops a particle on a solid cell. A world with no 3D colliders and no `ParticleGrid` pays only the empty span.

### wind-331 feat: sphere and disc emitters

Do:

- `EmitterShape::Sphere` samples the ellipsoid `shape_scale`.
- `EmitterShape::Disc` samples a disc in the plane whose normal is `direction`. `Circle` is unchanged.

Done when: `particle_test` places sphere samples inside the ellipsoid and disc samples on the plane. `Circle` samples still have `z = 0`.

### wind-332 feat: cull particle emitters

Do:

- Bounds from the live particles and the mode's extent. `run_render` and `render_world_viewport` skip an emitter no bound window's frustum hits. `frustum_cull` on the emitter defaults to true. `ParticleEmitter` is not a serialized component, so the flag is not a `.world` key.
- An empty particle list still pushes nothing.

Done when: `render_system_test` drops an emitter outside every frustum and keeps one that straddles. `frustum_cull = false` keeps today's always-draw. Update the [Scope](Scope.md) backlog line.

## Later, not these slices

Per-particle rigid rotation (a quaternion and a `vec3` spin) for tumbling debris. Soft particles against the scene depth. Lighting a billboard. Sorting individual particles against other transparent draws. Instancing of ordinary `Renderable`s.

## See also

- [Render](../modules/Render.md)
- [Scope](Scope.md)
- [Voxel 3D Plan](Voxel%203D%20Plan.md)
- [Lighting Plan](Lighting%20Plan.md)
- [Post Process Plan](Post%20Process%20Plan.md)
