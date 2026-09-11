# Render

Command buffer, materials, sprites, particles, and the OpenGL backend. Games never include glad or call `gl*`.

Draw order and `.mat` files: [Materials and Sort](../features/Materials%20and%20Sort.md).

## Commands

`Command` is a `std::variant` of four types (`include/engine/render/commands.h`):

| Command | Who pushes it |
| --- | --- |
| `CmdDrawMesh` | `run_render` and `render_world_viewport` for `Renderable` and `Sprite` |
| `CmdDrawUI` | `run_ui_render` for each `UiCanvas` |
| `CmdDrawParticles` | `run_render` and `render_world_viewport` for a `ParticleEmitter` whose `particles` list is not empty |
| `CmdSetView` | `run_render` (once per bound window) and `render_world_viewport` (for a sub-rectangle viewport) before world draws |

There is no custom-draw callback on the variant.

`CmdSetView` is the view state of the world draws that follow it: `view`, `projection`, `camera_position` (the translation of the camera's world matrix), `lights` (`SceneLights`, see [Lights](#lights)), and optional `viewport` (`std::optional<Rect>`, in top-left window coordinates) and `clear_color` (`std::optional<glm::vec4>`). It holds until the next `CmdSetView` in the same buffer. The draws carry no camera: `CmdDrawMesh` has only its `model`, and `CmdDrawParticles` has none (its instances are in world space). A draw before any `CmdSetView` in a buffer uses identity view and projection, camera position zero, no lights (`directional_count` 0, ambient black), full-window viewport, and no clear, which is a default-constructed `CmdSetView`; the OpenGL backend resets to that at the start of every `execute`. A test or tool that pushes its own `CmdDrawMesh` therefore draws in clip space unless it pushes a `CmdSetView` first.

`CommandBuffer` stores that variant and can `push`, `clear`, and be iterated. `IRenderBackend::execute` consumes one buffer. `ICanvas::draw` is the present hook the headless `Host` calls. The windowed path calls `draw_all` on the presentation instead; `OpenGLCanvas` splits it into `render`, `read_pixels` (only for a `wind-cli screenshot`), and `present`.

## Materials and meshes

`IMaterial` is shader, `texture(slot)`, color, and `render_state()`: a `RenderState` of `BlendMode` (`Opaque`, `Alpha`, `Additive`), `depth_test`, `depth_write`, and `CullMode` (`None`, `Back`, `Front`). `RenderState{}` is the 2D state (alpha, no depth, no culling). `Material` stores one albedo in slot 0. Any other slot returns null.

`MaterialOverride` replaces slot 0 and/or named `vec4` uniforms for one draw. `set_vec4` replaces the value when that name is already in `vec4_params`, and appends the name when it is not.

Instance tint is `material.color * instance.color` (`multiply_instance_color`). `Renderable::color` and `Sprite::color` are that instance color. Default is white, so the material color is unchanged.

`parse_material` reads a `.mat` TOML into `MaterialDesc` (`shader` and `[textures].albedo` are hex `AssetId` strings, plus color and `state`: `blend`, `depth_test`, `depth_write`, the `depth` shorthand, and `cull`; [Materials and Sort](../features/Materials%20and%20Sort.md)). Default blend in the desc is `Opaque`. The C++ `Material` constructor defaults its state to `RenderState{}`, whose blend is `Alpha`.

GPU objects are `IMesh`, `IShader`, `ITexture` created by `IGraphicFactory` from `MeshDesc`, `ShaderDesc`, and `TextureDesc`.

### Dynamic textures

`IGraphicFactory::create_dynamic_texture(DynamicTextureDesc)` returns an `IDynamicTexture` (an `ITexture`) that starts transparent black. `update(rgba, width, height)` replaces every pixel with RGBA8, top row first, the layout of `TextureDesc::rgba`, and the texture behaves exactly as if it had been made from that data (v = 1 is the top row). A new size reallocates and `width()` / `height()` follow. It returns false, and changes nothing, for a size that is not positive or an `rgba` shorter than `width * height * 4`. `OpenGLTexture` implements both: `update` flips the rows into one reused staging buffer (the same flip creation does), makes the engine's context current (`ContextScope`, as the factory does for creation), and calls `glTexSubImage2D`, or `glTexImage2D` when the size changed. Textures are shared between the windows' contexts, so one update shows in every window. Main thread only. A camera frame from [Capture](Capture.md) goes in with `texture->update(frame.rgba, frame.width, frame.height)`.

## Meshes

`MeshDesc` (`include/engine/render/graphic_factory.h`) is `vertices` plus `indices`. A `MeshVertex` is `position`, `normal` (default `{0, 0, 1}`), and `uv`. Empty `indices` is a non-indexed triangle list: every three vertices are one triangle. Otherwise every three `std::uint32_t` indices are one, each below `vertices.size()`. Front faces wind counter-clockwise; normals are written as given, so a lit shader expects unit length.

Vertex shader inputs are fixed: position at `location = 0`, uv at `1`, normal at `2`. A shader that reads only 0 and 1 (`builtin::shader_unlit`, every 2D shader) ignores the normal, so a quad draws as it did before normals existed. The particle draw has its own vertex buffer and locations 0 to 7; it does not use `MeshVertex`.

`mesh_desc_error` (`src/render/mesh_check.h`) names why a desc cannot be drawn: no vertices, an index count that is not a multiple of 3, or an index at or past the vertex count. `OpenGLMesh` logs that error and stays invalid, so `create_mesh` returns null and an asset load is `AssetError::Corrupt`. `parse_mesh` rejects the same descs. `OpenGLMesh` keeps one vertex buffer and, when there are indices, one element buffer, uploaded with no VAO bound. Each context's VAO binds the element buffer in its setup (the element binding is VAO state), and `draw` calls `glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, …)`, or `glDrawArrays` without indices. GLES 3.0 and WebGL2 accept 32-bit indices.

The `.mesh` file format is in [Assets](../features/Assets.md#mesh-files).

### Primitives

`include/engine/render/primitives.h` builds indexed `MeshDesc`s centred on the origin for `EngineServices::graphics.create_mesh`: counter-clockwise front faces, outward unit normals, UVs in [0, 1].

| Function | Shape |
| --- | --- |
| `make_quad()` | The unit quad of `builtin::mesh_quad`: XY plane, +Z normal, 4 vertices, 2 triangles |
| `make_cube(size = 1)` | Axis-aligned cube, 4 vertices per face (24, 36 indices) so each face has its own normal and 0..1 UVs. The geometry of `builtin::mesh_cube` |
| `make_plane(size = 1, subdivisions = 1)` | Square on the XZ plane, +Y normal, `subdivisions` (at least 1) cells per side. U along +X, V along -Z |
| `make_uv_sphere(radius = 0.5, segments = 32, rings = 16)` | `segments` (at least 3) around Y, `rings` (at least 2) pole to pole, a duplicated seam column. U wraps from +Z towards +X; V is 1 at the top. The triangle of a pole band that would touch the pole twice is left out |

`tests/primitives_test.cpp` checks that `quad.mesh` and `cube.mesh` parse to exactly `make_quad()` and `make_cube()`.

## World draw

`run_render` (`src/ecs/systems.cpp`) reads `ctx<BoundWindows>().ids` and any `UiCanvas` windows. `Worlds::bind_window` appends an id. `Worlds::unbind_window` removes one. `GameLoop` does not call `bind_window`. `EngineHost::attach_game` and the `Host` constructor bind `kPrimaryWindow`.

The function clears `ctx<WindowClears>()` first and clears each bound window's and `UiCanvas` window's command buffer once at the start of `Phase::Render` (recording cleared buffers in `WindowClears` so `run_ui_render` in `Phase::UiRender` does not clear them again, allowing custom `Phase::Render` systems such as `render_world_viewport` to append viewport draws before `Phase::UiRender` appends `CmdDrawUI`). When `BoundWindows` is empty, `run_render` returns before the camera check.

When `BoundWindows` is not empty, a window with no buffer is skipped: a bound window can outlive its buffer for a frame when the OS closes it between two dock syncs (`Worlds::unbind_window` runs on the next one). If there is then no `Renderable`, `Sprite`, or `ParticleEmitter`, it returns. An invalid or default `ctx<ActiveCamera>` is fatal, and so is a live entity that lacks `Camera` or `Transform`. Neither path pushes scene commands. `run_render` is only registered when `EngineSystemDeps` has a command buffer (`commands` or `commands_for_window`); a world without one draws nothing and never checks the camera. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again.

The draw list is sorted once. For every bound window that has a command buffer, `run_render` pushes one `CmdSetView` and then that list. Every window's view shares the camera's view matrix, camera position, and lights (collected once per frame by `collect_scene_lights`). Each window's projection is `projection_matrix` with `window_size_for(world, id)`, which reads `Presentation.sizes` for that id. A frame with nothing to draw pushes no `CmdSetView` either, so the buffer of a world with only UI is what it was before views existed.

`render_world_viewport(scene, target)` (`include/engine/ecs/systems.h`, `src/ecs/systems.cpp`) renders an arbitrary `ecs::World` (`scene`) into a sub-rectangle (`WorldViewportRender::viewport`, in top-left window coordinates) of `WorldViewportRender::commands` using an explicit `Camera` and `Transform` (`camera_transform`) instead of `scene.ctx<ActiveCamera>()`. It calls `ecs::update_world_transforms(scene)`, computes view and projection matrices for `viewport.w / viewport.h`, pushes a `CmdSetView` with `viewport` and `clear_color` (even when `scene` has no drawables, so an empty world still clears its viewport rectangle), and then pushes the sorted `CmdDrawMesh` and `CmdDrawParticles` commands collected from `scene`. When `target.fatal` is null (the default for editor preview viewports), entities whose mesh or material did not load are skipped without reporting a fatal error.

A null `Sprite::mesh` uses `builtin::mesh_quad` when that asset loads. An explicit `Sprite::material` is used as-is. A null material with a non-null `texture` reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer, or builds `Material(builtin::shader_unlit, texture, white, {.blend = BlendMode::Alpha})` and stores it. If that shader did not load and the cache misses, the sprite is fatal and skipped. `builtin::material_unlit` is not used on that path.

`builtin::material_unlit` is only the null-material and null-texture case, when that asset loads. A sprite that still has no mesh or material is fatal.

Items sort by `draw_sort_less` (`include/engine/render/draw_sort.h`): the opaque queue (opaque blend with depth write) first, by material, front to back, entity; then the transparent queue by layer, `order_in_layer`, depth-tested draws back to front, material, entity. A scene without depth-tested materials (every 2D scene) sorts by layer, order, material, entity. See [Materials and Sort](../features/Materials%20and%20Sort.md#sort). `sort_renderables(items, view)` is a stable sort with that key.

Model matrix is the entity's world matrix (`ecs::world_matrix`): translate, then rotate X, Y, Z, then scale for a root, and the parent chain times that for a child (`Parent`, see [ECS](ECS.md#hierarchy)). `Phase::Transform` refreshes it before `Render`. The sprite pivot, size and flip below are applied after it. A `Local`-space `ParticleEmitter` uses the same world matrix. The view is the inverse of the active camera's world matrix.

Sprite world size is `pixel_size / pixels_per_unit` (default 100), with the pivot offset, only when both `pixel_size` components and `pixels_per_unit` are greater than 0. `Sprite::pixel_size` defaults to `{0,0}`, so a sprite left there is the mesh at the `Transform` only. Flip and UV tiling/offset still apply. `get_sprite` fills `pixel_size`.

## Lights

ECS data, public, one type per header:

| Component | Header | Fields |
| --- | --- | --- |
| `DirectionalLight` | `include/engine/render/directional_light.h` | `color` (white), `intensity` (1) |
| `AmbientLight` | `include/engine/render/ambient_light.h` | `color` (white), `intensity` (0.2) |

`collect_scene_lights(world)` (`include/engine/render/lights.h`, `src/render/lights.cpp`) turns them into the `SceneLights` (`include/engine/render/scene_lights.h`) that `run_render` puts into every `CmdSetView` of the frame. All in world space:

- A directional light shines along the entity's world forward: the normalized -Z column of `ecs::world_matrix`. So `Transform::rotation` aims it, a `Parent` carries it (after `Phase::Transform`), and position and scale do not matter. An entity without `Transform` shines along -Z, and so does a matrix whose -Z column is zero. `SceneLights::colors[i]` is `color * intensity`.
- At most `kMaxDirectionalLights` (4) directional lights: the lowest entity indices (`Entity::operator<`), so the cut is the same every frame whatever the pool order. More than that logs one warning per process and the rest are ignored. The flag lives in `src/render/lights.cpp`, not in a header.
- Ambient is the sum of `color * intensity` over every `AmbientLight`, so two ambient lights add up and their order does not matter.
- A world with no `DirectionalLight` and no `AmbientLight` at all gets `default_scene_lights()`: one white light along `normalize(kDefaultLightDirection)` = `normalize(0.3, -1, -0.5)` (from above, in front, and to the left, so the visible faces of a cube seen from +Z all differ) and ambient `kDefaultAmbient` = 0.2. Without it a lit object in a scene that declares no lights would be black; with it a first 3D scene (camera, cube, `material_lit`) reads as a solid shape. The default is all or nothing: a world with any light entity gets exactly its lights, so a scene with only an `AmbientLight` has no directional light, and one with only directional lights has no ambient.

Unlit shaders ignore all of it, so lights never change a 2D game.

## Lit shader

`builtin::shader_lit` (`builtin_assets/shaders/lit.shader`, GLSL 330 core in CDATA, rewritten to 300 es by `adapt_glsl` like every shader) is Lambert diffuse plus ambient in world space:

```
albedo = texture(uTexture, vUV) * uColor
light  = uAmbient + sum over i < uLightCount of uLightColors[i] * max(dot(N, -uLightDirections[i]), 0)
FragColor = vec4(albedo.rgb * light, albedo.a)
```

`N` is `normalize(uNormalMatrix * aNormal)` (vertex location 2). The loop runs to the constant `kMaxDirectionalLights` (a `#define` of 4 in the shader, the same number as the C++ constant) and breaks at `uLightCount`, which GLSL ES 3.00 accepts. There is no specular term, so `uCameraPosition` is set but this shader does not read it.

`builtin::material_lit` (`builtin_assets/materials/lit.mat`) is that shader, white, no texture, `blend = "opaque"`, `depth = true`, `cull = "back"`, so it goes in the opaque queue.

Uniforms the backend sets for each mesh draw (the view and light ones also for each particle draw):

| Uniform | Type | Value |
| --- | --- | --- |
| `uModel` | `mat4` | `CmdDrawMesh::model` |
| `uNormalMatrix` | `mat3` | `transpose(inverse(mat3(model)))`, computed on the CPU only when the program has the uniform; identity when the 3x3 is singular (a zero scale) |
| `uView`, `uProjection` | `mat4` | current `CmdSetView` |
| `uCameraPosition` | `vec3` | current `CmdSetView::camera_position` |
| `uLightCount` | `int` | `lights.directional_count` |
| `uLightDirections[4]`, `uLightColors[4]` | `vec3` arrays | `lights.directions`, `lights.colors` (always all four elements) |
| `uAmbient` | `vec3` | `lights.ambient` |
| `uColor`, `uUvScale`, `uUvOffset`, `uTexture` | | as before ([Materials and Sort](../features/Materials%20and%20Sort.md#color)) |

`OpenGLShader` setters do nothing for a uniform the program does not have (location -1, which GL ignores; the new `set_mat3`, `set_vec3` and `set_vec3_array` check it explicitly), so `builtin::shader_unlit` and every 2D shader draw exactly as before.

## Sprites, animation, particles, curves

`Sprite` (`include/engine/render/sprite.h`) is a textured quad: color, layer, order, flip, tiling, offset, pixel size, pixels per unit, pivot, optional mesh, material, and override.

`SpriteAnimator` plays a `SpriteAnimationClip` and writes the current frame onto a sibling `Sprite` (`run_sprite_animations`, frame `delta_time`) through `Sprite::apply_frame`; `Sprite::from_frame` builds a sprite from a frame, and `set_param` / `clear_overrides` on `Sprite` and `Renderable` manage the per-instance `MaterialOverride` (see [Materials and Sort](../features/Materials%20and%20Sort.md)). Clips load from `importer = "animation"` TOML (`fps`, `loop`, frame list).

`ParticleEmitter` simulates in `run_particles` with frame `delta_time`. `run_render` pushes one `CmdDrawParticles` when `particles` is not empty. An empty list pushes nothing and does not report. When the list is not empty, a mesh or material that is still null is fatal and that entity is skipped.

Shapes are point, box, circle, and cone. Space is world or local. Playback is `play`, `pause`, `stop`, and `burst` (`include/engine/render/particles.h`).

Mesh and material, in `run_render`:

- `mesh`, or `builtin::mesh_quad` when `mesh` is null and that asset loads.
- An explicit `material` is used as-is. `CmdDrawParticles.blend` becomes that material's blend.
- A null `material` with a texture reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer (the same cache sprites use), or, when `builtin::shader_unlit` loaded, builds `Material(builtin::shader_unlit, texture, white, {.blend = emitter.blend})` and stores it.
- A null `material` and a null texture use `builtin::material_unlit` when it loads.
- In both null-material cases `CmdDrawParticles.blend` stays `emitter.blend` (default `Alpha`). The OpenGL backend applies `cmd.blend` for particles (`src/render/opengl/opengl_backend.cpp`).

When `collision_enabled` is set, `run_particles` copies non-trigger colliders into `ParticleCollider2D`s:

- `BoxCollider2D` becomes `Shape::Box`, with `box.size`.
- `CircleCollider2D` becomes `Shape::Circle`, with `circle.radius`.

`update_emitter` (`src/render/particles.cpp`) then tests the swept segment. These fields are on `ParticleEmitter`:

- A collider is skipped when `(layer & collision_mask) == 0`. `collision_mask` defaults to `0xFFFFFFFF`.
- `kill_on_collision` removes the particle. It does not bounce or apply `lifetime_loss`.
- Otherwise a positive `lifetime_loss` adds `lifetime * clamp(lifetime_loss, 0, 1)` to `age`.
- Velocity reflects with `bounce` and `friction` (each clamped to 0–1) when it points into the surface.
- `collision_radius` of 0 is a point. A positive value thickens the segment.

`update_emitter` takes an optional `JobSystem`. With none, or a pool that has no workers, integration runs on the caller. With workers, ranges of 64 particles run on the pool and the caller joins before it continues. Each range writes only its own particles. The caller then drops the dead ones and the survivors keep their order. Spawn stays on the caller: it draws the calling thread's random stream. A job does not call engine APIs.

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
| One VAO per GL context | `context_vao.cpp`, bookkeeping in `src/render/context_objects.cpp` |
| Canvas, font and image upload | `opengl_canvas.cpp` |
| UI device (`IUiDevice`) and its shader | `opengl_ui_device.cpp`, `opengl_ui_shader.h` |
| SDL window and GL context | `window_system.cpp` |
| Several windows | `window_manager.cpp` |
| Presentation | `sdl_gl_presentation.cpp` |

Desktop links glad and OpenGL 3.3. `ENGINE_WITH_GLES` links SDL without glad (Android also links GLESv3, EGL, `android`, and `log`).

Sampler state on upload comes from the catalog filter and wrap (`opengl_texture.cpp`).

View state (`opengl_backend.cpp`). `OpenGLRenderBackend` keeps one `CmdSetView` and records the context's full-window `GL_VIEWPORT` (`full_viewport_[4]`) at the start of `execute`. `execute` resets `view_` to a default `CmdSetView` (identity, no lights, full viewport) before the first command, a `CmdSetView` replaces it, and each mesh and particle draw sets `uView`, `uProjection`, `uCameraPosition`, and the light uniforms from it ([Lit shader](#lit-shader)). When `CmdSetView::viewport` is set (`Rect` in top-left window coordinates), `set_view` converts it to bottom-left OpenGL coordinates (`gl_y = full_viewport_[3] - (y + h)`), sets `glViewport` and `glScissor`, and enables `GL_SCISSOR_TEST`; when `viewport` is `std::nullopt`, it restores `full_viewport_` and disables `GL_SCISSOR_TEST`. When `CmdSetView::clear_color` is set, `set_view` enables depth writes, clears `GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT` to that color (clipped to the active scissor rect when `viewport` is set), and restores `glClearColor(0, 0, 0, 1)`. At the end of `execute`, the backend restores `full_viewport_` and disables `GL_SCISSOR_TEST`.

Albedo. A mesh draw binds the material's slot-0 texture on unit 0 or, when the material has none (or it is not valid), a 1x1 white RGBA texture the backend makes on first use (`white_texture`, shared by every context). So an untextured material draws as its color, under any shader. The particle draw has always done this; before wind-238 an untextured mesh sampled whatever unit 0 still held (the previous draw's texture, or nothing after the UI flush). A `MaterialOverride` albedo still replaces it for that draw.

Pipeline state of world draws (`opengl_backend.cpp`). Each `CmdDrawMesh` sets all of it from `material->render_state()`, so nothing leaks from the previous draw:

- Blend: `Opaque` disables `GL_BLEND`; `Alpha` is `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`; `Additive` is `SRC_ALPHA, ONE`.
- Depth: test and write both off disables `GL_DEPTH_TEST`. Otherwise `GL_DEPTH_TEST` is on with `glDepthFunc(GL_LEQUAL)` (an equal depth passes, so a second pass over the same surface draws), or `GL_ALWAYS` when only write is on (GL writes no depth while the test is disabled). `glDepthMask` follows `depth_write`.
- Cull: `None` disables `GL_CULL_FACE`; `Back` / `Front` enable it with `glFrontFace(GL_CCW)` and that `glCullFace`.
- `CmdDrawParticles` uses `cmd.blend` with depth test, depth write, and culling off, whatever its material says.

After the last command `OpenGLRenderBackend::execute` disables `GL_DEPTH_TEST` and `GL_CULL_FACE` and sets `glDepthMask(GL_TRUE)`. The UI flush that follows sets its own state anyway (no depth, stencil, cull). The mask matters for the next frame: `glClear` honours it, so a false mask would keep last frame's depth. `OpenGLCanvas::render` also sets `glDepthMask(GL_TRUE)` right before its `glClear(COLOR | STENCIL | DEPTH)`. State is per context: each window's canvas runs both in its own context, so every window starts its frame with a cleared depth buffer. The default framebuffer has a 24-bit depth buffer (`SDL_GL_DEPTH_SIZE`, `window_system.cpp`).

Each canvas's UI painter is a `ui::UiRenderer` (`src/ui/render/ui_renderer.h`): the backend-neutral `BatchingPainter` over the one `ui::PaintResources` the `OpenGLRenderBackend` keeps for every window (`ui_resources()`), with that canvas's own `OpenGLUiDevice`. `end_frame` catches the device's textures up with the shared atlas pages (`UiTextureSync`: a new page whole, a changed one its dirty rect, everything again after `PaintResources::generation` changes), uploads the frame's vertices and indices into two orphaned stream buffers, and issues one `glDrawElements` per batch, so the UI profiler's `draw_calls` is the batch count. The device is one program (GLSL 330 core, rewritten to 300 es by `adapt_glsl` under `ENGINE_WITH_GLES`) that implements `src/ui/render/fragment.h`, its own textures (R8 glyph and mask pages, RGBA8 ramp and image pages; bilinear, clamp, no mipmaps), and a VAO through `ContextVao`. It sets its whole pipeline when it draws and leaves what the rest of the backend expects: blend on with the premultiplied function, depth, stencil, cull, and scissor off, program, VAO, array buffer, and texture units 0 to 3 unbound with unit 0 active, unpack alignment 4. `ENGINE_GL_CHECKS` also checks for GL errors after the UI draw ("the UI draw"). `reset_ui_painter` (editor Stop) drops the shared images (`PaintResources::drop_images`), and the next frame registers and uploads the ones still referenced; fonts stay registered, being shared by windows that may be measuring text. The UI has no other painter. `fill_path` and `stroke_path` are coverage masks in that atlas, a stroke keyed by its device-pixel width as well as the path.

Every window has its own GL context. Textures, buffers, and programs are shared with the primary's context; vertex array objects are not. A VAO made in one context binds nothing, or another VAO, in the next, and the draw does nothing without an error. So `OpenGLMesh` and the particle draw keep their buffers once and make a VAO in each context the first time they draw there (`ContextVao`). The key of a context comes from `ContextObjects`, which `OpenGLCanvas` fills when it creates and destroys a context. A key is never reused, so a VAO cached for a destroyed context never binds in a new one whose driver handle is the same. A VAO released while another context is current is deleted the next time its own context renders. A destroyed context drops its VAOs with it.

GL errors are silent unless someone asks. With `ENGINE_GL_CHECKS` (Debug, RelWithDebInfo, the editor, and the Development player; not Shipping) `OpenGLRenderBackend::execute` drains `glGetError` before the first command and after each command and logs `GL_INVALID_OPERATION (0x0502) after CmdDrawParticles` once per error and command type (`GlErrorLog`, `src/render/gl_error_log.cpp`), so a draw that fails every frame writes one line, not one a frame. `OpenGLShader` logs the driver's info log when a shader fails to compile or a program fails to link, in every build.

`WindowSystem::set_icon` builds an `SDL_Surface` with `make_icon_surface` and calls `SDL_SetWindowIcon`. A null window or a short RGBA buffer yields a null surface. `tests/window_icon_test.cpp` covers the byte layout without `SDL_Init`.

## CPU sources (always)

- `src/render/material.cpp` — parse `.mat`
- `src/render/lights.cpp` — `collect_scene_lights`, `default_scene_lights`
- `src/render/material_instance.h` — `IMaterial` for a cooked material
- `src/render/animation.cpp`, `src/render/particles.cpp`, `src/render/shader_adapt.cpp`
- `src/render/context_objects.cpp` — GL context keys and deferred VAO deletes, without GL calls
- `src/render/gl_error_log.cpp` — GL error names and the once-per-place error log lines

## Public headers

- `include/engine/render/commands.h`
- `include/engine/render/command_buffer.h`
- `include/engine/render/directional_light.h`
- `include/engine/render/ambient_light.h`
- `include/engine/render/scene_lights.h`
- `include/engine/render/lights.h`
- `include/engine/render/renderable.h`
- `include/engine/render/draw_sort.h`
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

`tests/command_buffer_test.cpp`, `tests/lights_test.cpp` (light collection, lights and camera in `CmdSetView`), `tests/sort_test.cpp`, `tests/material_test.cpp`, `tests/render_system_test.cpp`, `tests/sprite_test.cpp`, `tests/animation_test.cpp`, `tests/particle_test.cpp`, `tests/shader_adapt_test.cpp`, `tests/opengl_texture_test.cpp` (CPU texture desc, no GL draw), `tests/ui_painter_test.cpp` (fake painter), `tests/window_icon_test.cpp`, `tests/context_objects_test.cpp` (context keys and deferred VAO deletes, no GL), `tests/gl_error_log_test.cpp`, `tests/ui_renderer_test.cpp` (UI renderer against a fake device: texture catch-up per device, draws per batch).

GPU pixels stay out of `engine_tests`. See [Boundaries](../architecture/Boundaries.md).

## See also

- [Materials and Sort](../features/Materials%20and%20Sort.md)
- [ECS](ECS.md)
