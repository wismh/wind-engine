# Materials and sort

World draws are data on entities. `run_render` turns them into commands, sorts them once, and pushes that list to every bound window's `CommandBuffer`.

## What you put on an entity

| Components | Draw |
| --- | --- |
| `Renderable` + `Transform` | one `CmdDrawMesh` |
| `Sprite` + `Transform` | one `CmdDrawMesh` (quad unless `mesh` is set) |
| `ParticleEmitter` | one `CmdDrawParticles` after `run_particles`, when `particles` is not empty |
| `SpriteAnimator` + `Sprite` | the animator copies the current frame onto the sprite first (`Sprite::apply_frame`) |

An entity with both `Renderable` and `Sprite` skips the sprite. `run_render` emits only the `Renderable` command (and nothing from that entity when the `Renderable` itself is skipped).

`Renderable` needs both `mesh` and `material`. A missing one is reported and that entity is skipped.

A `Sprite` with a null mesh uses `builtin::mesh_quad` when that asset loads. An explicit `material` is used as-is. A null material with a non-null `texture` reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer, or builds `Material(builtin::shader_unlit, texture, white, BlendMode::Alpha)` and caches it. If `builtin::shader_unlit` did not load and the cache misses, the sprite is fatal and skipped. `builtin::material_unlit` is only the null-material and null-texture case.

`MaterialOverride` on either component replaces albedo slot 0 and named `vec4` uniforms for that draw only. It is data on the entity: `Sprite::set_param(name, vec4)` and `Renderable::set_param` create the override on first use and set (or replace) one uniform, once; the value stays on the component until set again or `clear_overrides()`. Nothing rewrites it per frame, and nothing is shared between two entities that use one material: `run_render` copies the entity's own override into its `CmdDrawMesh::material_override`, and the material object is never written. The OpenGL backend sets the override uniforms for that draw and then restores what each uniform held before (`OpenGLShader::vec4_value`, `glGetUniformfv`), and unbinds an override albedo after the draw, so an override cannot leak into the next draw that uses the same shader program (a uniform stays in the program until it is set again). A shader without that uniform ignores it.

An empty `particles` list pushes nothing and does not report. When the list is not empty, mesh and material resolve as follows, and a result that is still null is fatal (`report` does not return).

- `mesh`, or `builtin::mesh_quad` when `mesh` is null and that asset loads.
- An explicit `material` is used as-is. `CmdDrawParticles.blend` becomes that material's blend.
- A null `material` with a texture reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer (the same cache sprites use), or, when `builtin::shader_unlit` loaded, builds `Material(builtin::shader_unlit, texture, white, emitter.blend)` and stores it.
- A null `material` and a null texture use `builtin::material_unlit` when it loads.
- In both null-material cases `CmdDrawParticles.blend` stays `emitter.blend` (default `Alpha`). The OpenGL backend applies `cmd.blend` for particles (`src/render/opengl/opengl_backend.cpp`).

`Sprite::from_frame(const SpriteAnimationFrame&)` builds a sprite from a frame, and `Sprite::apply_frame` copies onto an existing one, the same six frame-owned fields: `texture`, `tiling`, `offset`, `pixel_size`, `pixels_per_unit`, and `pivot`. Tint, layer, order, flips, `mesh`, `material`, and the override belong to the entity and are never touched. `run_sprite_animations` uses `apply_frame`, so the two cannot drift; `src/render/animation.cpp` binds all 14 `Sprite` members by name, which stops compiling when a field is added, so the choice "frame or entity" is made then. A `SpriteAnimator` that has not yet put a frame on its sprite (`frame_applied` is false: new, after `play` with another clip, after `stop`) does so on its next tick even when it is paused or `speed` is 0, so a spawned entity needs no manual copy. A paused animator that already applied its frame leaves the sprite alone.

## Color

`CmdDrawMesh.color` is the instance color (`Renderable::color` or `Sprite::color`). The backend multiplies it by `IMaterial::color`. White times the material color leaves the material color.

`tinted_color()` on the component is that product, for CPU-side use. The command stores the instance color, not the product.

## Matrices

Camera: `ctx<ActiveCamera>` holds one entity. When `ctx<BoundWindows>().ids` is empty, `run_render` returns before it clears a command buffer and before it reads the camera.

When the list is not empty and a `Renderable`, `Sprite`, or `ParticleEmitter` is present, each bound window's buffer is already cleared. An invalid or default entity then returns and does not call the fatal hook (`tests/render_system_test.cpp`, `SkipsScenePassWhenActiveCameraUnset`).

Fatal is only a live entity that lacks `Camera` or `Transform`. Either path pushes no scene commands. `run_ui_render` still pushes `CmdDrawUI`. Each bound window's projection is `projection_matrix` with `window_size_for` for that id (`Presentation.sizes`).

Model matrix, column vector, applied right to left as written in code: translate, rotate X, rotate Y, rotate Z, scale. Rotations are radians.

Sprite world size is `pixel_size / pixels_per_unit` (default 100). `pivot` is the origin inside that quad. That translate and scale run only when both `pixel_size` components and `pixels_per_unit` are greater than 0. `Sprite::pixel_size` defaults to `{0,0}` (`include/engine/render/sprite.h`), so a sprite left at that default is the mesh at the `Transform` only.

`flip_x` / `flip_y` still flip it. `tiling` and `offset` are the UV scale and UV offset. `get_sprite` fills `pixel_size`. Atlas frames from `SpriteSheet::get` fill `pixel_size`, `tiling`, and `offset`.

## Sort

Stable sort. The key, first difference wins:

1. `layer` ascending (low is behind)
2. `order_in_layer` ascending
3. `IMaterial*` address (`std::less`)
4. `entity.index` ascending

`include/engine/render/renderable.h` exports `renderable_less` and `sort_renderables` for a `Renderable` plus entity. `run_render` uses the same key for sprites and particles (`draw_item_less` in `src/ecs/systems.cpp`).

There is no Y-sort.

Particles and meshes share that list, so a particle batch participates in the same layer order. UI is not in this list. `run_ui_render` pushes `CmdDrawUI` after, per canvas `order`.

## `.mat`

TOML parsed by `parse_material`:

| Key | Default in `MaterialDesc` |
| --- | --- |
| `shader` | empty string |
| `blend` | `opaque` (`Opaque`, `Alpha`, `Additive`) |
| `color` | white |
| `[textures].albedo` | empty string |

`shader` and `[textures].albedo` are `AssetId` hex strings, parsed at load. A value that is not 32 lowercase hex characters makes the material `AssetError::Corrupt`. Omit `[textures]`, or leave that table empty, for no texture. A top-level `albedo` is ignored, so the material loads with no texture. A non-hex `textures.albedo` fails the parse. Codegen does not rewrite those strings.

## Where it executes

Headless tests can push commands and sort them. Pixels require `ENGINE_WITH_WINDOW`. `OpenGLBackend::execute` walks the buffer. `CmdDrawUI` goes to the NanoVG painter on that window's canvas.

`run_render` pushes those world commands to every id in `ctx<BoundWindows>()` that has a command buffer. A bound secondary window receives the same sorted draws. Its projection is `window_size_for` for that id (`Presentation.sizes`). `CmdDrawUI` is separate, per canvas.

## See also

- [Render](../modules/Render.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
- [Assets](Assets.md)
