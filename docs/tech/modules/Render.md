# Render

Command buffer, materials, sprites, particles, and the OpenGL backend. Games never include glad or call `gl*`.

Draw order and `.mat` files: [Materials and Sort](../features/Materials and Sort.md).

## Commands

`Command` is a `std::variant` of three types (`include/engine/render/commands.h`):

| Command | Who pushes it |
| --- | --- |
| `CmdDrawMesh` | `run_render` for `Renderable` and `Sprite` |
| `CmdDrawParticles` | `run_render` for a `ParticleEmitter` whose `particles` list is not empty |
| `CmdDrawUI` | `run_ui_render` for each `UiCanvas` |

There is no custom-draw callback on the variant.

`CommandBuffer` stores that variant and can `push`, `clear`, and be iterated. `IRenderBackend::execute` consumes one buffer. `ICanvas::draw` is the present hook the headless `Host` calls. The windowed path calls `draw_all` on the presentation instead.

## Materials and meshes

`IMaterial` is shader, `texture(slot)`, color, and `BlendMode` (`Opaque`, `Alpha`, `Additive`). `Material` stores one albedo in slot 0. Any other slot returns null.

`MaterialOverride` replaces slot 0 and/or named `vec4` uniforms for one draw. `set_vec4` replaces the value when that name is already in `vec4_params`, and appends the name when it is not.

Instance tint is `material.color * instance.color` (`multiply_instance_color`). `Renderable::color` and `Sprite::color` are that instance color. Default is white, so the material color is unchanged.

`parse_material` reads a `.mat` TOML into `MaterialDesc` (`shader` and `[textures].albedo` are hex `AssetId` strings, plus blend and color; [Materials and Sort](../features/Materials and Sort.md)). Default blend in the desc is `Opaque`. The C++ `Material` constructor defaults blend to `Alpha` when the caller does not pass one.

GPU objects are `IMesh`, `IShader`, `ITexture` created by `IGraphicFactory` from `MeshDesc`, `ShaderDesc`, and `TextureDesc`.

## World draw

`run_render` (`src/ecs/systems.cpp`) reads `ctx<BoundWindows>().ids`. `Worlds::bind_window` appends an id. `Worlds::unbind_window` removes one. `GameLoop` does not call `bind_window`. `Engine::init` and the `Host` constructor bind `kPrimaryWindow`.

An empty list returns before any command buffer is cleared and before the camera check. The function clears `ctx<WindowClears>()` first. That set only records which buffers were cleared this frame.

When the list is not empty, each bound window's command buffer is cleared once. A window with no buffer is skipped. If there is then no `Renderable`, `Sprite`, or `ParticleEmitter`, it returns. An invalid or default `ctx<ActiveCamera>` returns after those clears and does not call the fatal hook. A live entity that lacks `Camera` or `Transform` is fatal. Neither path pushes scene commands. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again.

The draw list is sorted once. That list is pushed to every bound window that has a command buffer. Every window shares the camera view. Each window's projection is `projection_matrix` with `window_size_for(world, id)`, which reads `Presentation.sizes` for that id.

A null `Sprite::mesh` uses `builtin::mesh_quad` when that asset loads. An explicit `Sprite::material` is used as-is. A null material with a non-null `texture` reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer, or builds `Material(builtin::shader_unlit, texture, white, BlendMode::Alpha)` and stores it. If that shader did not load and the cache misses, the sprite is fatal and skipped. `builtin::material_unlit` is not used on that path.

`builtin::material_unlit` is only the null-material and null-texture case, when that asset loads. A sprite that still has no mesh or material is fatal and skipped.

Items sort by layer, then `order_in_layer`, then material pointer, then entity index (`renderable_less` / `draw_item_less`). `sort_renderables` is a stable sort of that predicate.

Model matrix is translate, then rotate X, Y, Z, then scale.

Sprite world size is `pixel_size / pixels_per_unit` (default 100), with the pivot offset, only when both `pixel_size` components and `pixels_per_unit` are greater than 0. `Sprite::pixel_size` defaults to `{0,0}`, so a sprite left there is the mesh at the `Transform` only. Flip and UV tiling/offset still apply. `get_sprite` fills `pixel_size`.

## Sprites, animation, particles, curves

`Sprite` (`include/engine/render/sprite.h`) is a textured quad: color, layer, order, flip, tiling, offset, pixel size, pixels per unit, pivot, optional mesh, material, and override.

`SpriteAnimator` plays a `SpriteAnimationClip` and writes the current frame onto a sibling `Sprite` (`run_sprite_animations`, frame `delta_time`). Clips load from `importer = "animation"` TOML (`fps`, `loop`, frame list).

`ParticleEmitter` simulates in `run_particles` with frame `delta_time`. `run_render` pushes one `CmdDrawParticles` when `particles` is not empty. An empty list pushes nothing and does not report. When the list is not empty, a mesh or material that is still null is fatal and that entity is skipped.

Shapes are point, box, circle, and cone. Space is world or local. Playback is `play`, `pause`, `stop`, and `burst` (`include/engine/render/particles.h`).

Mesh and material, in `run_render`:

- `mesh`, or `builtin::mesh_quad` when `mesh` is null and that asset loads.
- An explicit `material` is used as-is. `CmdDrawParticles.blend` becomes that material's blend.
- A null `material` with a texture reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer (the same cache sprites use), or, when `builtin::shader_unlit` loaded, builds `Material(builtin::shader_unlit, texture, white, emitter.blend)` and stores it.
- A null `material` and a null texture use `builtin::material_unlit` when it loads.
- In both null-material cases `CmdDrawParticles.blend` stays `emitter.blend` (default `Alpha`). The OpenGL backend applies `cmd.blend` for particles (`src/render/opengl/opengl_backend.cpp`).

When `collision_enabled` is set, `run_particles` copies non-trigger colliders into `ParticleCollider`s:

- `BoxCollider` becomes `Shape::Box`, with `box.size`.
- `CircleCollider` becomes `Shape::Circle`, with `circle.radius`.

`update_emitter` (`src/render/particles.cpp`) then tests the swept segment. These fields are on `ParticleEmitter`:

- A collider is skipped when `(layer & collision_mask) == 0`. `collision_mask` defaults to `0xFFFFFFFF`.
- `kill_on_collision` removes the particle. It does not bounce or apply `lifetime_loss`.
- Otherwise a positive `lifetime_loss` adds `lifetime * clamp(lifetime_loss, 0, 1)` to `age`.
- Velocity reflects with `bounce` and `friction` (each clamped to 0–1) when it points into the surface.
- `collision_radius` of 0 is a point. A positive value thickens the segment.

`Curve<T>` (`include/engine/render/curve.h`) is a keyed curve. `KeyInterpolation` is `Linear`, `Smooth`, or `Step`. On the emitter, evaluated over normalized lifetime:

- A non-empty `size_curve_xy` (`Curve<glm::vec2>`) wins over `size_curve` (`Curve<float>`). When both are empty, size stays on the linear start/end.
- A non-empty `color_curve` (`Curve<glm::vec4>`) replaces the linear start/end color.
- A non-empty `alpha_curve` (`Curve<float>`) then overwrites alpha.

## Shaders

Desktop target is `ShaderTarget::Glsl330Core`. GLES (web and Android) uses `Glsl300Es`. `adapt_glsl` / `adapt_shader` rewrite a GLSL 330 source for ES (`include/engine/render/shader_adapt.h`). `.shader` files are XML with the source in CDATA (`parse_shader_xml` in `src/resources/importers.h`).

## OpenGL (`ENGINE_WITH_WINDOW`)

These sources are not in the headless library. CMake adds them only when the window flag is on.

| Piece | File |
| --- | --- |
| Factory | `src/render/opengl/opengl_factory.cpp` |
| Mesh, shader, texture | `opengl_mesh.cpp`, `opengl_shader.cpp`, `opengl_texture.cpp` |
| Execute mesh and particle draws | `opengl_backend.cpp` |
| Canvas, font and image upload | `opengl_canvas.cpp` |
| UI painter | `nanovg_painter.cpp` (`IUiPainter`) |
| SDL window and GL context | `window_system.cpp` |
| Several windows | `window_manager.cpp` |
| Presentation | `sdl_gl_presentation.cpp` |

Desktop links glad and OpenGL 3.3. `ENGINE_WITH_GLES` links SDL and NanoVG without glad (Android also links GLESv3, EGL, `android`, and `log`).

Sampler state on upload comes from the catalog filter and wrap (`opengl_texture.cpp`).

`WindowSystem::set_icon` builds an `SDL_Surface` with `make_icon_surface` and calls `SDL_SetWindowIcon`. A null window or a short RGBA buffer yields a null surface. `tests/window_icon_test.cpp` covers the byte layout without `SDL_Init`.

## CPU sources (always)

- `src/render/material.cpp` — parse `.mat`
- `src/render/material_instance.h` — `IMaterial` for a cooked material
- `src/render/animation.cpp`, `src/render/particles.cpp`, `src/render/shader_adapt.cpp`

## Public headers

- `include/engine/render/commands.h`
- `include/engine/render/command_buffer.h`
- `include/engine/render/renderable.h`
- `include/engine/render/sprite.h`
- `include/engine/render/material.h`
- `include/engine/render/graphics.h`
- `include/engine/render/graphic_factory.h`
- `include/engine/render/canvas.h`
- `include/engine/render/backend.h`
- `include/engine/render/animation.h`
- `include/engine/render/particles.h`
- `include/engine/render/curve.h`
- `include/engine/render/shader_adapt.h`

## Tests

`tests/command_buffer_test.cpp`, `tests/sort_test.cpp`, `tests/material_test.cpp`, `tests/render_system_test.cpp`, `tests/sprite_test.cpp`, `tests/animation_test.cpp`, `tests/particle_test.cpp`, `tests/shader_adapt_test.cpp`, `tests/opengl_texture_test.cpp` (CPU texture desc, no GL draw), `tests/ui_painter_test.cpp` (fake painter), `tests/window_icon_test.cpp`.

GPU pixels stay out of `engine_tests`. See [Boundaries](../architecture/Boundaries.md).

## See also

- [Materials and Sort](../features/Materials and Sort.md)
- [ECS](ECS.md)
