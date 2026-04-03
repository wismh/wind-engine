# Materials and sort

World draws are data on entities. `run_render` turns them into commands, sorts them once, and pushes that list to every bound window's `CommandBuffer`, after one `CmdSetView` per window that carries the camera and the lights ([Render](../modules/Render.md#commands)).

## What you put on an entity

| Components | Draw |
| --- | --- |
| `Renderable` + `Transform` | one `CmdDrawMesh` |
| `Sprite` + `Transform` | one `CmdDrawMesh` (quad unless `mesh` is set) |
| `ParticleEmitter` | one `CmdDrawParticles` after `run_particles`, when `particles` is not empty |
| `SpriteAnimator` + `Sprite` | the animator copies the current frame onto the sprite first (`Sprite::apply_frame`) |

An entity with both `Renderable` and `Sprite` skips the sprite. `run_render` emits only the `Renderable` command (and nothing from that entity when the `Renderable` itself is skipped).

`Renderable` needs both `mesh` and `material`. A missing one is reported and that entity is skipped.

A `Sprite` with a null mesh uses `builtin::mesh_quad` when that asset loads. An explicit `material` is used as-is. A null material with a non-null `texture` reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer, or builds `Material(builtin::shader_unlit, texture, white, {.blend = BlendMode::Alpha})` and caches it. If `builtin::shader_unlit` did not load and the cache misses, the sprite is fatal and skipped. `builtin::material_unlit` is only the null-material and null-texture case.

`MaterialOverride` on either component replaces albedo slot 0 and named `vec4` uniforms for that draw only. It is data on the entity: `Sprite::set_param(name, vec4)` and `Renderable::set_param` create the override on first use and set (or replace) one uniform, once; the value stays on the component until set again or `clear_overrides()`. Nothing rewrites it per frame, and nothing is shared between two entities that use one material: `run_render` copies the entity's own override into its `CmdDrawMesh::material_override`, and the material object is never written. The OpenGL backend sets the override uniforms for that draw and then restores what each uniform held before (`OpenGLShader::vec4_value`, `glGetUniformfv`), and unbinds an override albedo after the draw, so an override cannot leak into the next draw that uses the same shader program (a uniform stays in the program until it is set again). A shader without that uniform ignores it.

An empty `particles` list pushes nothing and does not report. When the list is not empty, mesh and material resolve as follows, and a result that is still null is fatal (`report` does not return).

- `mesh`, or `builtin::mesh_quad` when `mesh` is null and that asset loads.
- An explicit `material` is used as-is. `CmdDrawParticles.blend` becomes that material's blend.
- A null `material` with a texture reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer (the same cache sprites use), or, when `builtin::shader_unlit` loaded, builds `Material(builtin::shader_unlit, texture, white, {.blend = emitter.blend})` and stores it.
- A null `material` and a null texture use `builtin::material_unlit` when it loads.
- In both null-material cases `CmdDrawParticles.blend` stays `emitter.blend` (default `Alpha`). The OpenGL backend applies `cmd.blend` for particles (`src/render/opengl/opengl_backend.cpp`).

`Sprite::from_frame(const SpriteAnimationFrame&)` builds a sprite from a frame, and `Sprite::apply_frame` copies onto an existing one, the same six frame-owned fields: `texture`, `tiling`, `offset`, `pixel_size`, `pixels_per_unit`, and `pivot`. Tint, layer, order, flips, `mesh`, `material`, and the override belong to the entity and are never touched. `run_sprite_animations` uses `apply_frame`, so the two cannot drift; `src/render/animation.cpp` binds all 14 `Sprite` members by name, which stops compiling when a field is added, so the choice "frame or entity" is made then. A `SpriteAnimator` that has not yet put a frame on its sprite (`frame_applied` is false: new, after `play` with another clip, after `stop`) does so on its next tick even when it is paused or `speed` is 0, so a spawned entity needs no manual copy. A paused animator that already applied its frame leaves the sprite alone.

## Render state

`IMaterial::render_state()` returns a `RenderState` (`include/engine/render/material.h`): `blend` (`BlendMode`), `depth_test`, `depth_write`, and `cull` (`CullMode`: `None`, `Back`, `Front`; counter-clockwise is the front face, as the wind-236 primitives wind). `RenderState{}` is the 2D state: `Alpha`, no depth test, no depth write, `None`. `Material(shader, albedo, color, state)` takes it whole; the cached sprite and particle materials pass `RenderState{.blend = ...}` and so keep the 2D state. A 3D opaque material is `{.blend = Opaque, .depth_test = true, .depth_write = true, .cull = Back}`; `builtin::material_lit` is one, on the Lambert `builtin::shader_lit` ([Render](../modules/Render.md#lit-shader)).

The backend reads the state from `CmdDrawMesh::material` at execute time; the command carries no copy. Particles ignore everything but the blend: `CmdDrawParticles` draws with `cmd.blend`, no depth test or write, and no culling ([Render](../modules/Render.md#opengl-engine_with_window)).

## Color

`CmdDrawMesh.color` is the instance color (`Renderable::color` or `Sprite::color`). The backend multiplies it by `IMaterial::color`. White times the material color leaves the material color.

`tinted_color()` on the component is that product, for CPU-side use. The command stores the instance color, not the product.

A material with no albedo samples a 1x1 white texture in the OpenGL backend, so the draw is `uColor` (times the lighting under `shader_lit`), never black or a stale texture.

## Matrices

Camera: `ctx<ActiveCamera>` holds one entity. When `ctx<BoundWindows>().ids` is empty, `run_render` returns before it clears a command buffer and before it reads the camera.

When the list is not empty and a `Renderable`, `Sprite`, or `ParticleEmitter` is present, each bound window's buffer is already cleared. An invalid or default entity then returns and does not call the fatal hook (`tests/render_system_test.cpp`, `SkipsScenePassWhenActiveCameraUnset`).

Fatal is only a live entity that lacks `Camera` or `Transform`. Either path pushes no scene commands. `run_ui_render` still pushes `CmdDrawUI`. Each bound window's projection is `projection_matrix` with `window_size_for` for that id (`Presentation.sizes`). View and projection reach the backend only in that window's `CmdSetView`, pushed before its draws; `CmdDrawMesh` carries the model matrix alone and `CmdDrawParticles` no matrix.

Model matrix, column vector, applied right to left as written in code: translate, rotate (`mat4_cast` of the `Transform::rotation` quaternion), scale.

Sprite world size is `pixel_size / pixels_per_unit` (default 100). `pivot` is the origin inside that quad. That translate and scale run only when both `pixel_size` components and `pixels_per_unit` are greater than 0. `Sprite::pixel_size` defaults to `{0,0}` (`include/engine/render/sprite.h`), so a sprite left at that default is the mesh at the `Transform` only.

`flip_x` / `flip_y` still flip it. `tiling` and `offset` are the UV scale and UV offset. `get_sprite` fills `pixel_size`. Atlas frames from `SpriteSheet::get` fill `pixel_size`, `tiling`, and `offset`.

## Sort

Stable sort, once per frame, on a `DrawSortKey` per draw (`include/engine/render/draw_sort.h`, `draw_sort_less`). `run_render` builds the keys with `make_draw_sort_key` (`draw_item_less` in `src/ecs/systems.cpp` only forwards). Every window shares the camera view, so one sort serves them all.

Queue first. `draw_queue(state)` is **Opaque** when the material's blend is `Opaque` and `depth_write` is on, **Transparent** otherwise. Every opaque draw goes before every transparent one, whatever its layer. A particle batch is always transparent.

View depth is the distance in front of the camera: minus the view-space z of the draw's world position (the translation of its model matrix, after the sprite pivot; the emitter's world position for a local-space emitter, the origin for a world-space one). It is computed once per draw.

Opaque queue, first difference wins:

1. `IMaterial*` address (`std::less`), to batch state
2. view depth ascending (front to back, so the depth test rejects hidden fragments early)
3. `entity.index` ascending

Transparent queue:

1. `layer` ascending (low is behind)
2. `order_in_layer` ascending
3. depth-sorted draws before the others
4. among depth-sorted draws, view depth descending (back to front)
5. `IMaterial*` address
6. `entity.index` ascending

**The 2D rule.** A draw is depth-sorted only when its material has `depth_test` on (and it is not a particle batch, which never depth-tests). No 2D material does: `RenderState{}`, `.mat` defaults, the sprite and particle caches, and `builtin::material_unlit` all leave depth test off. So in a 2D scene every draw is transparent and not depth-sorted, and the key is exactly layer, order, material, entity, as before wind-237: sprites at different z with the same layer and order still draw in material, then entity order, under an orthographic or a perspective camera (`RenderSystem.TwoDimensionalSpritesAtDifferentZKeepMaterialThenEntityOrder`). The rule does not look at the camera's projection. Within one layer and order, a sprite that is not depth-tested draws after the depth-tested transparent draws, so it lands on top of them.

`include/engine/render/renderable.h` keeps `sort_renderables(items, view)` for a `RenderableItem` (a `Renderable`, its entity, and its `world_position`), a stable sort with the same key (`draw_sort_key(item, view)`).

There is no Y-sort.

Particles and meshes share that list, so a particle batch participates in the same layer order. UI is not in this list. `run_ui_render` pushes `CmdDrawUI` after, per canvas `order`.

## `.mat`

TOML parsed by `parse_material`:

| Key | Default in `MaterialDesc` |
| --- | --- |
| `shader` | empty string |
| `blend` | `opaque` (`Opaque`, `Alpha`, `Additive`) |
| `color` | white |
| `depth_test` | `false` |
| `depth_write` | `false` |
| `depth` | `false`; shorthand that sets `depth_test` and `depth_write` together. An explicit `depth_test` or `depth_write` overrides it |
| `cull` | `none` (`none`, `back`, `front`) |
| `[textures].albedo` | empty string |

`blend`, `depth*`, and `cull` fill `MaterialDesc::state`. An unknown `blend` or `cull` string fails the parse, as does a `depth`, `depth_test`, or `depth_write` that is not a TOML boolean (an integer `1` is not accepted). A 3D opaque material is `blend = "opaque"`, `depth = true`, `cull = "back"`.

`shader` and `[textures].albedo` are `AssetId` hex strings, parsed at load. A value that is not 32 lowercase hex characters makes the material `AssetError::Corrupt`. Omit `[textures]`, or leave that table empty, for no texture. A top-level `albedo` is ignored, so the material loads with no texture. A non-hex `textures.albedo` fails the parse. Codegen does not rewrite those strings.

## Where it executes

Headless tests can push commands and sort them. Pixels require `ENGINE_WITH_WINDOW`. `OpenGLBackend::execute` walks the buffer. `CmdDrawUI` goes to the UI renderer on that window's canvas.

`run_render` pushes those world commands to every id in `ctx<BoundWindows>()` that has a command buffer, each list after that window's `CmdSetView`. A bound secondary window receives the same sorted draws. Its projection, in its own `CmdSetView`, is `window_size_for` for that id (`Presentation.sizes`). The backend keeps the last `CmdSetView` while it walks the buffer and starts each `execute` from identity matrices. `CmdDrawUI` is separate, per canvas.

## See also

- [Render](../modules/Render.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Assets](Assets.md)
