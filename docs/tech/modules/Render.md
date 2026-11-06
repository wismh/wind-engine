# Render

Command buffer, materials, sprites, particles, and the OpenGL backend. Games never include glad or call `gl*`.

Draw order and `.mat` files: [Materials and Sort](../features/Materials%20and%20Sort.md).

## Commands

`Command` is a `std::variant` of five types (`include/engine/render/commands.h`):

| Command | Who pushes it |
| --- | --- |
| `CmdDrawMesh` | `run_render` and `render_world_viewport` for each `Renderable` and `Sprite` inside a window's frustum ([Frustum culling](#frustum-culling)) |
| `CmdDrawUI` | `run_ui_render` for each `UiCanvas` |
| `CmdDrawParticles` | `run_render` and `render_world_viewport` for a `ParticleEmitter` whose `particles` list is not empty and whose bounds meet a bound window's frustum, unless `frustum_cull` is false |
| `CmdSetView` | `run_render` (once per bound window) and `render_world_viewport` (for a sub-rectangle viewport) before world draws |
| `CmdPostProcess` | `run_render`, after that window's world draws, when the view's post chain is active |

There is no custom-draw callback on the variant.

`CmdSetView` is the view state of the world draws that follow it: `view`, `projection`, `camera_position` (the translation of the camera's world matrix), `lights` (`SceneLights`, see [Lights](#lights)), optional `fog` (`std::optional<Fog>`, see [Fog](#fog)), `post` (`PostChain`, see [Post process](#post-process)), and optional `viewport` (`std::optional<Rect>`, in top-left window coordinates) and `clear_color` (`std::optional<glm::vec4>`). It holds until the next `CmdSetView` in the same buffer. The draws carry no camera: `CmdDrawMesh` has only its `model`, and `CmdDrawParticles` has none (its instances are in world space). A draw before any `CmdSetView` in a buffer uses identity view and projection, camera position zero, no lights (`count` 0, ambient black), no fog, full-window viewport, and no clear, which is a default-constructed `CmdSetView`; the OpenGL backend resets to that at the start of every `execute`. A test or tool that pushes its own `CmdDrawMesh` therefore draws in clip space unless it pushes a `CmdSetView` first.

`CommandBuffer` stores that variant and can `push`, `clear`, and be iterated. `IRenderBackend::execute` consumes one buffer. `ICanvas::draw` is the present hook the headless `Host` calls. The windowed path calls `draw_all` on the presentation instead; `OpenGLCanvas` splits it into `render`, `read_pixels` (only for a `wind-cli screenshot`), and `present`.

## Materials and meshes

`IMaterial` is shader, `texture(slot)` (slot 0 may be a 2D texture or a [texture array](#texture-arrays)), color, and `render_state()`: a `RenderState` of `BlendMode` (`Opaque`, `Alpha`, `Additive`), `depth_test`, `depth_write`, `CullMode` (`None`, `Back`, `Front`), and `alpha_cutoff` (`std::optional<float>`, see [Cutout](#cutout)). `RenderState{}` is the 2D state (alpha, no depth, no culling, no cutoff). `Material` stores one albedo in slot 0. Any other slot returns null.

`MaterialOverride` replaces slot 0 and/or named `vec4` uniforms for one draw. `set_vec4` replaces the value when that name is already in `vec4_params`, and appends the name when it is not.

Instance tint is `material.color * instance.color` (`multiply_instance_color`). `Renderable::color` and `Sprite::color` are that instance color. Default is white, so the material color is unchanged.

`parse_material` reads a `.mat` TOML into `MaterialDesc` (`shader` and `[textures].albedo` are `asset://` URIs of an `AssetId`; the albedo may be a texture array, plus color and `state`: `blend`, `depth_test`, `depth_write`, the `depth` shorthand, and `cull`; [Materials and Sort](../features/Materials%20and%20Sort.md)). Default blend in the desc is `Opaque`. The C++ `Material` constructor defaults its state to `RenderState{}`, whose blend is `Alpha`.

GPU objects are `IMesh`, `IShader`, `ITexture` created by `IGraphicFactory` from `MeshDesc`, `ShaderDesc`, and `TextureDesc`.

### Dynamic textures

`IGraphicFactory::create_dynamic_texture(DynamicTextureDesc)` returns an `IDynamicTexture` (an `ITexture`) that starts transparent black. `update(rgba, width, height)` replaces every pixel with RGBA8, top row first, the layout of `TextureDesc::rgba`, and the texture behaves exactly as if it had been made from that data (v = 1 is the top row). A new size reallocates and `width()` / `height()` follow. It returns false, and changes nothing, for a size that is not positive or an `rgba` shorter than `width * height * 4`. `OpenGLTexture` implements both: `update` flips the rows into one reused staging buffer (the same flip creation does), makes the engine's context current (`ContextScope`, as the factory does for creation), and calls `glTexSubImage2D`, or `glTexImage2D` when the size changed. Textures are shared between the windows' contexts, so one update shows in every window. Main thread only. A camera frame from [Capture](Capture.md) goes in with `texture->update(frame.rgba, frame.width, frame.height)`. A dynamic texture has no mip levels.

`update_rects(rects, rgba, row_stride)` writes rectangles and leaves every other pixel. `rgba` is the same top-row-first layout, and `row_stride` is that image's width in pixels, so the buffer may be wider than one rectangle. `TextureRect` is the top-left (`y = 0` is the top row) plus the size. An empty list changes nothing and returns true. It returns false, changes nothing, and logs the reason, when a rectangle is empty or outside the texture, `row_stride` does not cover it, or `rgba` ends before the last byte it reads (`texture_rects_error`, `src/render/texture_check.h`). A texture array or a texture with mipmaps returns false without a log: a partial write would leave the other mip levels stale. The size does not change.

Both backends place a rectangle so its bottom row lands at GPU row `height - (y + height)` (`texture_rect_gpu_y`), the same row a full `update` would give that rectangle. OpenGL binds once, flips each rectangle into the reused staging buffer, and calls `glTexSubImage2D`. Vulkan keeps the CPU copy (bottom row first, the buffer a later full upload sends). When that image is already the current size and nothing is waiting for a full upload, one command buffer copies every rectangle, transitioning level 0 from `SHADER_READ_ONLY` to `TRANSFER_DST` and back — not from `UNDEFINED`, which would discard the pixels outside the rectangles. Otherwise the rectangles stay in the CPU copy and the next `ensure_gpu_image` uploads the whole image. The copy waits on `vkQueueWaitIdle`, as a full upload does. A partial update never resizes, so it does not destroy the image a frame in flight may still sample.

### Mipmaps

`TextureDesc::mipmaps` and `TextureArrayDesc::mipmaps` are a `MipmapMode` (`include/engine/resources/meta.h`): `None` (the default: level 0 only, exactly as before mipmaps existed), `Nearest`, or `Linear`, the filter between mip levels. `FilterMode` stays the filter inside a level: it is the magnification filter and the base of the minification filter, so `filter = Nearest` with `mipmaps = Linear` is `GL_NEAREST_MIPMAP_LINEAR` (crisp pixels, smooth level blend). The `.meta` key is `mipmaps = "none" | "nearest" | "linear"` ([Resources](Resources.md#meta)). A level count is `mip_level_count(w, h)` = floor(log2(max(w, h))) + 1 (`src/render/texture_check.h`).

- OpenGL (`OpenGLTexture::apply_sampling`): the min filter is `GL_{NEAREST|LINEAR}_MIPMAP_{NEAREST|LINEAR}`, and after the upload `glGenerateMipmap` builds the chain (core in GL 3.0, GLES 3.0 and WebGL2; per layer for an array).
- Vulkan (`VulkanTexture::upload_pixels`): the image has the full chain and `TRANSFER_SRC` usage; level 0 is copied for every layer, then each level is a `VK_FILTER_LINEAR` `vkCmdBlitImage` of the one above (`R8G8B8A8_UNORM` with optimal tiling always supports blits and linear filtering), each source level moving to `SHADER_READ_ONLY` once read. The sampler's `mipmapMode` follows the mode and `maxLod` is the last level (0 without mipmaps).

A mipmapped atlas still bleeds between neighbouring tiles at small levels; a block game puts one face per layer of a texture array instead.

### Texture arrays

`IGraphicFactory::create_texture_array(TextureArrayDesc)` (`include/engine/render/graphic_factory.h`) makes one texture of `layers` images of `width` x `height`; `rgba` is layer 0 first, each layer top row first like `TextureDesc::rgba`, so v = 1 is a layer's top row. It returns null, logging why, for a size or layer count that is not positive, more than `kMaxTextureArrayLayers` (256, the minimum GL 3.3, GLES 3.0, and Vulkan all guarantee; no device query), or an `rgba` shorter than `width * height * 4 * layers` (`texture_array_desc_error`, `src/render/texture_check.h`, checked before any GL or Vulkan call). The result is an `ITexture` whose `layers()` is the count and whose `width()` / `height()` are one layer's; `layers()` is 0 for every 2D texture. An array is never dynamic: `update` on one returns false.

The layer comes from the mesh: `MeshVertex::layer` (a `float` at `location = 4`, default 0). Not `uv`'s third component, which would change `uv` from `glm::vec2` everywhere (primitives, tilemaps, sprites, `.mesh`), and not a per-material uniform, because one voxel chunk mesh is one draw whose faces need different layers. The shader passes it as a `flat` varying (Slang `nointerpolation`) and samples `texture(uTexture, vec3(uv, layer))`; GL rounds the layer to the nearest integer and clamps it to the array. Give all three vertices of a triangle the same layer. 2D, sprite, tilemap, and particle shaders never declare location 4. `.mesh` files have no layer column, so their vertices load with layer 0.

`builtin::shader_lit_array` (`builtin_assets/shaders/lit_array.shader`) and `builtin::shader_unlit_array` (`unlit_array.shader`) are `shader_lit` and `shader_unlit` with `uniform sampler2DArray uTexture` (Slang `Sampler2DArray`) sampled at the vertex layer: vertex color, cutout and fog included, the same uniforms, each declared in one stage. `shader_unlit_array` has no animated tiles (it does not read location 2). There is no builtin array material; a game writes a `.mat` whose `shader` is one of them and whose `[textures].albedo` is a `texture_array` asset ([Assets](../features/Assets.md#texture-arrays)).

Binding. A material's slot 0 (or a `MaterialOverride` albedo) may hold an array. The sampler type in the shader decides what is read: a draw whose albedo is the other kind (an array under a `sampler2D` shader, a 2D texture under a `sampler2DArray` one) samples white, the same rule as a draw with no albedo.

- OpenGL (`OpenGLRenderBackend::bind_albedo`): every mesh and particle draw binds both targets of unit 0, the albedo on its own target (`GL_TEXTURE_2D` or `GL_TEXTURE_2D_ARRAY`) and a white texture (`white_texture()` or a 1x1x1 `white_texture_array()`) on the other. A program only reads the target its sampler type names, so no shader introspection is needed; the cost is one extra `glBindTexture` per draw.
- Vulkan: `SpirvReflection::sampler_arrayed` is the `Arrayed` operand of the sampler's `OpTypeImage`. `resolve_albedo(material, override, arrayed)` returns the albedo when `(layers() > 0) == arrayed`, otherwise `white_texture_` or a one-layer `white_texture_array_`, so the descriptor's view type (`VK_IMAGE_VIEW_TYPE_2D` or `2D_ARRAY`) always matches the shader. Particles always resolve a 2D albedo.
- GLSL ES 3.00 has no default precision for `sampler2DArray`, so `adapt_glsl` inserts `precision mediump sampler2DArray;` into any stage that uses one and does not declare its precision.

## Meshes

`MeshDesc` (`include/engine/render/graphic_factory.h`) is `vertices` plus `indices`, and optionally `influences` (one `SkinInfluence` per vertex: four `uint8` joint indices and four weights) and `bounds_override`. A `MeshVertex` is `position`, `normal` (default `{0, 0, 1}`), `uv`, `color` (`glm::vec4`, default white), and `layer` (`float`, default 0, the [texture array](#texture-arrays) layer): 52 bytes. Empty `indices` is a non-indexed triangle list: every three vertices are one triangle. Otherwise every three `std::uint32_t` indices are one, each below `vertices.size()`. Empty `influences` is a static mesh. A count that does not match the vertices is rejected by `mesh_desc_error`. Front faces wind counter-clockwise; normals are written as given, so a lit shader expects unit length.

Vertex shader inputs are fixed: position at `location = 0`, uv at `1`, normal at `2`, color at `3`, layer at `4`. Both backends always feed those five, and a shader reads what it declares. A mesh with influences also feeds joints at location 5 (`uvec4`) and weights at location 6 (`vec4`), on a second vertex buffer. Locations 0–4 of a static mesh stay as they are. `builtin::shader_lit` and `builtin::shader_unlit` multiply the albedo by the vertex color (baked light and ambient occlusion in a block world, or a tint). `builtin::shader_unlit` reads location 2 as `aAnim`, the animated-tile parameters the tilemap baker packs into `normal` (`aAnim.x <= 1` is a static tile, so a plain quad's +Z normal is not animated). `make_*` primitives, the tilemap baker, and `.mesh` rows without color all write white (and layer 0), so nothing drawn before vertex color existed changes. The particle draw has its own vertex buffer and locations 0 to 7; it does not use `MeshVertex`.

`mesh_desc_error` (`src/render/mesh_check.h`) names why a desc cannot be drawn: no vertices, an index count that is not a multiple of 3, or an index at or past the vertex count. `OpenGLMesh` logs that error and stays invalid, so `create_mesh` returns null and an asset load is `AssetError::Corrupt`. `parse_mesh` rejects the same descs. `OpenGLMesh` keeps one vertex buffer and, when there are indices, one element buffer, uploaded with no VAO bound. Each context's VAO binds the element buffer in its setup (the element binding is VAO state), and `draw` calls `glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, …)`, or `glDrawArrays` without indices. GLES 3.0 and WebGL2 accept 32-bit indices.

### Mesh updates

`IMesh::update(const MeshDesc&)` (`include/engine/render/graphics.h`, `src/render/mesh.cpp`) replaces every vertex and index of a live mesh, as if it had been made from that desc. It is not virtual: it checks `mesh_desc_error` first and, on an error, logs it and returns false without calling the backend, so the mesh keeps what it had. A valid desc goes to the protected `IMesh::replace`, which each backend implements; false from it also leaves the old contents. Only after `replace` returns true does `update` set the [bounds](#mesh-bounds) from the new desc, so a failed update keeps the old bounds with the old contents. There is no separate dynamic mesh type: any mesh, asset meshes included, can be updated. Main thread only. A test fake implements `replace`.

`OpenGLMesh::replace` makes the engine's context current (`ContextScope`, with the activator `OpenGLFactory` passes in) and writes in place with `glBufferSubData` when the data fits the buffer's storage. When it does not, it calls `glBufferData` on the same buffer name with `GL_DYNAMIC_DRAW` and room for `max(needed, 1.5 × old)` bytes, then `glBufferSubData`. Storage never shrinks. Keeping the names keeps every context's VAO valid (VAO state refers to buffer names, not storage), so a reallocation needs no rebind. The element buffer is written with no VAO bound, like creation. A non-indexed mesh that gains indices makes its first element buffer and releases its VAOs (`ContextVao::release`), so each context builds one with the element binding on its next draw. A mesh that drops its indices keeps the element buffer and draws with `glDrawArrays`. The same code runs on desktop GL, GLES 3.0, and WebGL2.

`VulkanMesh::replace` never writes buffers a frame in flight may read. Without a device, or before its first upload, it only swaps the CPU copies that `ensure_gpu_buffers` uploads later. Otherwise it uploads new host-visible vertex and index buffers (no queue wait); if either upload fails it frees them and returns false with the old buffers in place. On success it hands the old buffers to `VulkanContext::retire_buffer` and uses the new ones. The destructor retires its buffers the same way. See [Vulkan](#vulkan-engine_with_window) for the retire queue.

The `.mesh` file format is in [Assets](../features/Assets.md#mesh-files).

### Skinning

`CmdDrawMesh::bones` is the palette for this draw, and `bone_count` is its length. Both empty is the static path: quads, tiles, and sprites are unchanged, and `lit` / `unlit` are not skinned. `run_render` takes the palette from the primitive's `ModelAnimator`. A skinned mesh with no palette is fatal, the same as a mesh with no material.

`builtin::shader_skin_lit` and `builtin::shader_skin_unlit` are the lit and 3D unlit shaders with the skin in the vertex stage: `skin = Σ weight[i] * uBones[joint[i]]`, and both the position and the normal go through that matrix. The palette is a uniform block (`Bones` in the GLSL block, `uBones` in the Slang the Vulkan reflection reads), not an array in the default uniform block. OpenGL binds that block only for a skinned draw. Vulkan writes the `mat4` array into the reflected UBO. The block holds 128 matrices (`kMaxSkinJoints`).

`IMesh::bounds()` is `bounds_override` when the desc set one, otherwise the box of the vertices. A cooked skinned mesh stores the union of its posed vertices there, so a character in an extreme pose stays inside the frustum test.

### Mesh bounds

`IMesh::bounds()` is a `render::Aabb` (`include/engine/render/aabb.h`: `min`, `max`) in the mesh's local space: `bounds_override` when the desc set one, otherwise the box around every vertex position of the desc the mesh was made or last updated from, `mesh_bounds(desc)` (`include/engine/render/graphic_factory.h`). Vertices no index uses still count, so no index walk is needed and the box is never too small. A desc without vertices gives a zero box at the origin (`create_mesh` returns null for it anyway). The base class owns it: `IMesh` has one protected constructor, `explicit IMesh(const MeshDesc&)`, which `OpenGLMesh`, `VulkanMesh` and every test fake call, and the non-virtual `update` refreshes it, so no backend computes bounds and they cannot disagree. A test fake built by a fake factory passes the factory's desc; a standalone fake passes `make_quad()`.

### Primitives

`include/engine/render/primitives.h` builds indexed `MeshDesc`s centred on the origin for `EngineServices::graphics.create_mesh`: counter-clockwise front faces, outward unit normals, UVs in [0, 1].

| Function | Shape |
| --- | --- |
| `make_quad()` | The unit quad of `builtin::mesh_quad`: XY plane, +Z normal, 4 vertices, 2 triangles |
| `make_cube(size = 1)` | Axis-aligned cube, 4 vertices per face (24, 36 indices) so each face has its own normal and 0..1 UVs. The geometry of `builtin::mesh_cube` |
| `make_plane(size = 1, subdivisions = 1)` | Square on the XZ plane, +Y normal, `subdivisions` (at least 1) cells per side. U along +X, V along -Z |
| `make_uv_sphere(radius = 0.5, segments = 32, rings = 16)` | `segments` (at least 3) around Y, `rings` (at least 2) pole to pole, a duplicated seam column. U wraps from +Z towards +X; V is 1 at the top. The triangle of a pole band that would touch the pole twice is left out |

Every primitive leaves `color` white. `tests/primitives_test.cpp` checks that, and that `quad.mesh` and `cube.mesh` parse to exactly `make_quad()` and `make_cube()`.

## World draw

`run_render` (`src/ecs/systems.cpp`) reads `ctx<BoundWindows>().ids` and any `UiCanvas` windows. `Worlds::bind_window` appends an id. `Worlds::unbind_window` removes one. `GameLoop` does not call `bind_window`. `EngineHost::attach_game` and the `Host` constructor bind `kPrimaryWindow`.

The function clears `ctx<WindowClears>()` first and clears each bound window's and `UiCanvas` window's command buffer once at the start of `Phase::Render` (recording cleared buffers in `WindowClears` so `run_ui_render` in `Phase::UiRender` does not clear them again, allowing custom `Phase::Render` systems such as `render_world_viewport` to append viewport draws before `Phase::UiRender` appends `CmdDrawUI`). When `BoundWindows` is empty, `run_render` returns before the camera check.

When `BoundWindows` is not empty, a window with no buffer is skipped: a bound window can outlive its buffer for a frame when the OS closes it between two dock syncs (`Worlds::unbind_window` runs on the next one). If there is then no `Renderable`, `Sprite`, or `ParticleEmitter`, it returns. An invalid or default `ctx<ActiveCamera>` is fatal, and so is a live entity that lacks `Camera` or `Transform`. Neither path pushes scene commands. `run_render` is only registered when `EngineSystemDeps` has a command buffer (`commands` or `commands_for_window`); a world without one draws nothing and never checks the camera. `run_ui_render` still pushes `CmdDrawUI`. A window `run_render` already cleared is not cleared again.

The draw list is collected, [culled](#frustum-culling) and sorted once. For every bound window that has a command buffer, `run_render` pushes one `CmdSetView` and then that list. Every window's view shares the camera's view matrix, camera position, and lights (collected once per frame by `collect_scene_lights`). Each window's projection is `projection_matrix` with `window_size_for(world, id)`, which reads `Presentation.sizes` for that id. A frame with nothing to draw pushes no `CmdSetView` either, so the buffer of a world with only UI is what it was before views existed.

`render_world_viewport(scene, target)` (`include/engine/ecs/systems.h`, `src/ecs/systems.cpp`) renders an arbitrary `ecs::World` (`scene`) into a sub-rectangle (`WorldViewportRender::viewport`, in top-left window coordinates) of `WorldViewportRender::commands` using an explicit `Camera` and `Transform` (`camera_transform`) instead of `scene.ctx<ActiveCamera>()`. It calls `ecs::update_world_transforms(scene)`, computes view and projection matrices for `viewport.w / viewport.h`, pushes a `CmdSetView` with `viewport` and `clear_color` (even when `scene` has no drawables, so an empty world still clears its viewport rectangle), and then pushes the sorted `CmdDrawMesh` and `CmdDrawParticles` commands collected from `scene`. When `target.fatal` is null (the default for editor preview viewports), entities whose mesh or material did not load are skipped without reporting a fatal error.

A null `Sprite::mesh` uses `builtin::mesh_quad` when that asset loads. An explicit `Sprite::material` is used as-is. A null material with a non-null `texture` reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer, or builds `Material(builtin::shader_unlit, texture, white, {.blend = BlendMode::Alpha})` and stores it. If that shader did not load and the cache misses, the sprite is fatal and skipped. `builtin::material_unlit` is not used on that path.

`builtin::material_unlit` is only the null-material and null-texture case, when that asset loads. A sprite that still has no mesh or material is fatal.

Items sort by `draw_sort_less` (`include/engine/render/draw_sort.h`): the opaque queue (opaque blend with depth write) first, by material, front to back, entity; then the transparent queue by layer, `order_in_layer`, depth-tested draws back to front, material, entity. A scene without depth-tested materials (every 2D scene) sorts by layer, order, material, entity. See [Materials and Sort](../features/Materials%20and%20Sort.md#sort). `sort_renderables(items, view)` is a stable sort with that key.

Model matrix is the entity's world matrix (`ecs::world_matrix`): translate, then rotate X, Y, Z, then scale for a root, and the parent chain times that for a child (`Parent`, see [ECS](ECS.md#hierarchy)). `Phase::Transform` refreshes it before `Render`. The sprite pivot, size and flip below are applied after it. A `Local`-space `ParticleEmitter` uses the same world matrix. The view is the inverse of the active camera's world matrix.

Sprite world size is `pixel_size / pixels_per_unit` (default 100), with the pivot offset, only when both `pixel_size` components and `pixels_per_unit` are greater than 0. `Sprite::pixel_size` defaults to `{0,0}`, so a sprite left there is the mesh at the `Transform` only. Flip and UV tiling/offset still apply. `get_sprite` fills `pixel_size`.

## Frustum culling

`run_render` and `render_world_viewport` skip a `Renderable` or `Sprite` whose mesh lies outside the view. The math is public and pure:

| Function | Header | Result |
| --- | --- | --- |
| `transform_aabb(box, transform)` | `include/engine/render/aabb.h` | The box around `box` after an affine matrix: the center through the matrix, the half extents through its absolute 3x3. Rotation only grows it; a flip or zero scale keeps `min <= max` |
| `make_frustum(view_projection)` | `include/engine/render/frustum.h` | `Frustum::planes`: left, right, bottom, top, near, far of the clip volume -w <= x, y, z <= w, normalized, normals inward, in the space the matrix maps from (world for `projection * view`) |
| `frustum_intersects(frustum, box)` | same | False only when the whole box is behind one plane (its farthest corner along the normal is), beyond a relative tolerance of 1e-5. A box touching a plane is inside. Conservative near the corners of the volume |
| `visible_in_any(frustums, box)` | same | Any of them intersects; an empty span is always true (no culling) |
| `camera_frustum(camera, camera_world, window)` | `include/engine/ecs/camera.h` | `make_frustum(projection_matrix(camera, window) * view_matrix(camera_world))` |

A plane that is not finite (a NaN or infinite matrix, an orthographic camera on a 0-wide window) becomes all zero and keeps everything, and a NaN distance compares as inside, so a degenerate view never culls. Near and far are tested too. The CPU frustum is the OpenGL clip volume, -w <= z <= w. Vulkan clips z to [0, w], so the backend left-multiplies `uProjection` by the remap `z_vk = 0.5 * z_gl + 0.5 * w` (`vulkan_projection`) before writing the uniform. xy and w are unchanged, and the CPU matrix used for culling and picking stays the OpenGL one. With the default 2D camera (near -1, far 1) an entity more than one unit in z from the camera was already outside both volumes.

What is tested, in `collect_world_draws` (`src/ecs/systems.cpp`):

- A `Renderable`: `transform_aabb(mesh->bounds(), world matrix)` (`ecs::world_matrix`, so a `Parent` counts).
- A `Sprite`: the same box under the final draw model, the world matrix with the pivot offset, the `pixel_size / pixels_per_unit` scale and the flip applied, so size, pivot and flip are exact. The mesh is `Sprite::mesh` or `builtin::mesh_quad`.
- Tilemap chunks: the chunk's cell rectangle under the map's model, before the chunk is baked ([Tilemaps](#tilemaps)).
- A `ParticleEmitter`: the union of its live particles, at the world position the command already writes. `Plane` is half `|size|` on XY and 0 on Z (a rotated quad can stick out). `Billboard` and `Stretched` are a radius of half the longer side on every axis; that sphere does not cover the quad corner. `Mesh` is `transform_aabb(mesh->bounds(), translate(position) * scale(mesh_scale * size factor))`, so an off-center local box and a negative scale are included. `frustum_cull` defaults to true and is not a `.world` key.
- Not culled: UI, and draws a tool pushes itself, such as the editor's gizmo and outline.

The fatal checks come first: a `Renderable`, `Sprite`, or `ParticleEmitter` with no mesh or material is fatal whether or not it would be on screen. An emitter with an empty particle list is not fatal and pushes nothing.

Windows. `run_render` builds one `camera_frustum` per bound window (`window_size_for` that id) and keeps a draw that any of them sees; the one sorted list still goes to every window. The windows of a world share its camera and view and differ only in aspect, so a per-window list would save draws only in the strip between two aspects. `render_world_viewport` culls with the frustum of its own camera and viewport size, so the editor's scene view culls against the editor camera; its `CmdSetView` and clear are pushed even when everything is culled.

Opt-out. `Renderable::frustum_cull`, `Sprite::frustum_cull`, and `ParticleEmitter::frustum_cull` (default true). False always draws the entity: for a shader that moves vertices beyond the tested box (vertex displacement, a sway, a sky, a card shader that leaves the CPU particle box). `Renderable` and `Sprite` are serialized as `frustum_cull` in `.world` (read optionally, default true). `ParticleEmitter` is not a serialized component, so its flag is not a `.world` key. A mesh whose vertices change (a voxel chunk) needs nothing: `IMesh::update` keeps its bounds current.

## Tilemaps

`Tilemap` (`include/engine/render/tilemap.h`) draws a grid of tiles from one tileset texture. Data lives in a `.tilemap` asset (`importer = "tilemap"`, TOML):

```toml
texture = "<guid of the tileset png>"   # a texture asset; use filter = "nearest" in its .meta for pixel art
size = [70, 40]                          # cells
tile_size = [16, 16]                     # px per tile in the texture
columns = 4                              # optional; default texture width / tile_size.x
pixels_per_unit = 16                     # optional, default 100: cell size in world units = tile_size / pixels_per_unit

[[layers]]
name = "ground"
tiles = [0, 1, 2, ...]                   # size.x * size.y, row-major, row 0 on top; 0 empty, n is tile n - 1
```

`parse_tilemap` rejects a non-GUID texture, a non-positive size, a layer whose `tiles` is not `size.x * size.y` long, and a tile outside 0..65535; `asset_codegen` runs it and fails the build (`CodegenErrorKind::Tilemap`). `AssetsDb::try_get<TilemapAsset>` loads the texture too and resolves `columns`.

The entity's `Transform` places the map: cell (0, 0) is the top-left corner at the origin, x grows right, y grows up, so the map extends down from the origin. `Tilemap::layer` and `order_in_layer` sort it like a `Sprite`; map layer i draws at `order_in_layer + i`. The `tilemap` serializer entry stores the asset GUID, `color`, `layer` and `order_in_layer` (in `.world`); cells come from the asset.

`run_render` and `render_world_viewport` cut each layer into 32 x 32 cell chunks. A chunk is one baked `IMesh` (a quad per non-empty cell, UVs baked) drawn as one `CmdDrawMesh` with the shared per-texture material from `SpriteMaterialCache`, so there is no new `Command`. A chunk is built when it first becomes visible, and only chunks whose cell rectangle, under the map's model (`transform_aabb`), intersects the frustum of a bound window are drawn (`visible_tilemap_chunks` takes one world-space `Frustum` per window, see [Frustum culling](#frustum-culling); the render viewport passes its own). `Tilemap::set_tile` edits the component's copy of the cells and marks only the chunk that holds the cell dirty. The next `visible_tilemap_chunks` that returns that chunk bakes it again and calls `IMesh::update` on its mesh, so an edit reuses the chunk's mesh and GPU buffers; `create_mesh` runs only for a chunk that has no mesh yet or whose update failed. A chunk left with no tile drops its mesh. Edits are not saved. Tile index to UV follows `SpriteSheet::get` (image rows run down, V runs up); there is no half-texel inset, so use nearest filtering and a texture without spacing.

Not covered yet: tile collision, flips, animated tiles, Y-sort, and an editor painting tool.

## Lights

Decision record: [Lighting Plan](../architecture/Lighting%20Plan.md). One forward list for meshes and for sprites that opt in.

ECS data, public, one type per header:

| Component | Header | Fields |
| --- | --- | --- |
| `DirectionalLight` | `include/engine/render/directional_light.h` | `color` (white), `intensity` (1) |
| `AmbientLight` | `include/engine/render/ambient_light.h` | `color` (white), `intensity` (0.2) |
| `PointLight` | `include/engine/render/point_light.h` | `color` (white), `intensity` (1), `range` (10) |
| `SpotLight` | `include/engine/render/spot_light.h` | those, plus `inner_angle` (25°), `outer_angle` (35°), radians from the axis |

`collect_scene_lights` (`include/engine/render/lights.h`, `src/render/lights.cpp`) turns them into the `SceneLights` (`include/engine/render/scene_lights.h`) that `run_render` puts into every `CmdSetView` of the frame. The three-argument overload takes the frame's frustums and the camera position; `run_render` and `render_world_viewport` use it. The one-argument overload culls nothing and measures distance from the origin. All in world space:

- A directional light shines along the entity's world forward: the normalized -Z column of `ecs::world_matrix`. So `Transform::rotation` aims it, a `Parent` carries it (after `Phase::Transform`), and position and scale do not matter. An entity without `Transform` shines along -Z, and so does a matrix whose -Z column is zero. `SceneLight::color` is `color * intensity`. A spot aims the same way and also has a world position. A point uses only the position.
- The view holds one array, `kMaxForwardLights` (16). Directional lights come first, at most `kMaxDirectionalLights` (4), lowest entity index. Point and spot fill the rest: the range sphere must meet a passed frustum (an empty span keeps them), nearest to the camera, entity index breaks a tie. Past either cap logs one warning per process. The flags live in `src/render/lights.cpp`.
- Ambient is the sum of `color * intensity` over every `AmbientLight`. It is not an entry in the array.
- A world with no light component at all gets `default_scene_lights()`: one white directional along `normalize(kDefaultLightDirection)` = `normalize(0.3, -1, -0.5)` and ambient `kDefaultAmbient` = 0.2. Any light component turns that rig off, including a point that the frustum then drops, so a scene with only an `AmbientLight` has no directional light.
- `pack_forward_lights` is the GPU layout: `uLightColorKind` (rgb color, w kind), `uLightDirRange` (xyz travel direction, w range), `uLightPosSpot` (xyz position, w spot scale), `uLightSpotBias` (x is the cone bias). Kind is 0 directional, 1 point, 2 spot. The cone is `saturate(dot(-L, direction) * scale + bias)`, 1 at the inner angle and 0 at the outer. An inner angle that is not smaller than the outer angle is a hard edge at the outer angle. Point and spot attenuation is `saturate(1 - (d/range)^2)^2 / (d^2 + 1)`.

Unlit shaders do not declare the list, so a sprite on `material_unlit` is unchanged. `material_sprite_lit` (`shader_sprite_lit`) reads the same list with Lambert off, so a light in the sprite's plane still shades it. A normal map on slot 1 turns Lambert on for that sprite. Tilemaps, card particles, and UI are not lit. Mesh particles use the material shader, so `shader_lit` lights them.

## Lit shader

`builtin::shader_lit` (`builtin_assets/shaders/lit.shader`, GLSL 330 core in CDATA, rewritten to 300 es by `adapt_glsl` like every shader) shades with `forward_shade` from `builtin_assets/shaders/.forward_light.glsl` (and the slang twin). The shader cooker pastes that file when the stage says `#include ".forward_light.glsl"`, from the shader's own directory, before it writes the blob. The leading dot keeps the snippet out of the asset scan. `lit_array` and `skin_lit` include it too. `sprite_lit` includes it and passes Lambert off unless `uHasNormalMap` is set.

```
albedo = texture(uTexture, vUV) * uColor * vColor
if (albedo.a < uAlphaCutoff) discard
N      = normalize(vNormal), perturbed by uNormal when uHasNormalMap != 0
color  = forward_shade(albedo.rgb, N, vWorldPos, lambert)
if (uFogEnabled != 0) color = mix(uFogColor, color, fog factor of length(vViewPos))
FragColor = vec4(color, albedo.a)
```

`forward_shade` adds ambient, then each light. A directional has no falloff. A point and a spot use the windowed falloff above. A spot also multiplies by its cone. Lambert is `max(dot(N, L), 0)` when the caller asks for it, and 1 when it does not. `uGloss` above 0 adds a Blinn-Phong term on the Lambert path; 0 adds none. `vWorldPos` is `uModel * position` (skinned first when the shader skins). `vViewPos` stays the view-space position, so fog needs no uniform in both stages.

`builtin::shader_lit_array` and `builtin::shader_unlit_array` are the same two shaders sampling a texture array at `MeshVertex::layer` ([Texture arrays](#texture-arrays)).

`builtin::shader_unlit` is the same without lighting: `color = texture(uTexture, vUV) * uColor * vColor`, the same cutout test and the same fog, plus the animated-tile UV offset from `aAnim` and `uTime`.

GLSL ES requires a uniform declared in both stages to have the same precision; the vertex stage defaults to highp and `adapt_glsl` gives the fragment stage mediump. So each uniform of the builtin shaders is declared in one stage only (`Assets.BuiltinShadersDeclareNoUniformInBothStages`).

`builtin::material_lit` (`builtin_assets/materials/lit.mat`) is that shader, white, no texture, `blend = "opaque"`, `depth = true`, `cull = "back"`, so it goes in the opaque queue.

Uniforms the backend sets for each mesh draw (the view and light ones also for each particle draw):

| Uniform | Type | Value |
| --- | --- | --- |
| `uModel` | `mat4` | `CmdDrawMesh::model` |
| `uNormalMatrix` | `mat3` | `transpose(inverse(mat3(model)))`, computed on the CPU only when the program has the uniform; identity when the 3x3 is singular (a zero scale) |
| `uView`, `uProjection` | `mat4` | current `CmdSetView` |
| `uCameraPosition` | `vec3` | current `CmdSetView::camera_position` |
| `uLightCount` | `int` | `lights.count` |
| `uLightColorKind[16]`, `uLightDirRange[16]`, `uLightPosSpot[16]`, `uLightSpotBias[16]` | `vec4` arrays | `pack_forward_lights` (always all 16 elements) |
| `uAmbient` | `vec3` | `lights.ambient` |
| `uGloss` | `float` | `IMaterial::gloss()`, 0 when the material does not override it |
| `uHasNormalMap`, `uNormal` | `int`, `sampler2D` | 1 and slot 1 when the material has a normal map, else 0 and a white texture |
| `uAlphaCutoff` | `float` | `material->render_state().alpha_cutoff`, 0 when unset (mesh draws only) |
| `uFogEnabled` | `int` | 1 when `CmdSetView::fog` is set, else 0 |
| `uFogColor`, `uFogRange` | `vec3`, `vec2` | `fog->color` and `(fog->start, fog->end)`; white and (0, 100) without fog |
| `uColor`, `uUvScale`, `uUvOffset`, `uTexture` | | as before ([Materials and Sort](../features/Materials%20and%20Sort.md#color)) |

`OpenGLShader` setters do nothing for a uniform the program does not have (location -1, which GL ignores; the new `set_mat3`, `set_vec3` and `set_vec3_array` check it explicitly), so `builtin::shader_unlit` and every 2D shader draw exactly as before.

## Cutout

`RenderState::alpha_cutoff` is alpha test. The backend sets `uAlphaCutoff` for every mesh draw to the cutoff, or 0 when there is none, and the builtin lit and unlit shaders discard a fragment whose albedo alpha (texture times material color times vertex color) is below it. Alpha is never below 0, so 0 is "off" and needs no flag; a zero-initialized Vulkan uniform block is off too. A `.mat` sets it with `alpha_cutoff = 0.5` ([Materials and Sort](../features/Materials%20and%20Sort.md#mat)).

Cutout does not change the queue: an opaque material with depth write and a cutoff (leaves, fences, grass) stays in the opaque queue, sorts front to back, and writes depth for the fragments it keeps. The cutoff is a uniform, not a shader variant, so the `discard` is in the builtin shaders whether a cutoff is set or not. On some GPUs a shader that can discard loses early depth testing for every draw that uses it, cutout or not; that is the cost of one lit and one unlit shader. Particles ignore the cutoff.

## Fog

`render::Fog` (`include/engine/render/fog.h`) is linear distance fog: `color` (white), `start` (0), `end` (100). There is no exponential mode. Put it on the `ActiveCamera` entity: `run_render` copies it into every window's `CmdSetView::fog`. Without the component `fog` is empty and nothing is fogged; a `Fog` on any other entity is ignored. It is serialized as the `fog` world component (`color`, `start`, `end`).

The builtin lit and unlit shaders mix the fragment's color towards `color` by `clamp((end - d) / max(end - start, 0.0001), 0, 1)`, where `d` is the fragment's distance from the camera (the length of its view-space position, so fog does not change as the camera turns). An `end` at or before `start` is a hard edge at `end`. Alpha is not fogged, and an additive material fades to the fog color, not to black.

Fogged: everything drawn with `builtin::shader_lit` or `builtin::shader_unlit` (or their array variants), sprites and tilemaps included, but only under a camera that has `Fog`, so a 2D game is unchanged. Not fogged: particles (their builtin GL and Slang shaders have no fog; a custom particle shader receives the fog uniforms and may use them), UI, and `render_world_viewport`, which pushes no fog (the editor's scene viewport is unfogged). Both backends set the fog uniforms for mesh and particle draws from the current view ([Lit shader](#lit-shader)).

## Post process

`render::PostStack` (`include/engine/render/post_process.h`) sits on the `ActiveCamera` entity, the same place as `Fog`. `hdr` asks for a float scene target and a builtin tonemap after the list. `effects` is an ordered list of `PostEffect`: a shader `AssetId`, `params` (`vec4`, default white), and `sample_depth`. No component, or `hdr == false` with an empty list, draws the world into the window and allocates nothing. `hdr == true` with an empty list still renders to the float target and tonemaps. The tonemap is not an entry the game adds.

`run_render` calls `prepare_post_stack` before it loads shaders. Bloom (`builtin::shader_post_bloom`) with `hdr == false` is fatal (`"Bloom post effect requires PostStack::hdr"`). The depth builtin (`builtin::shader_post_depth`) forces `sample_depth`. A prepared stack is then `resolve_post_chain`, which loads each shader and, when `hdr` is set, `builtin::shader_post_tonemap`. A missing shader is fatal. The chain is copied onto every bound window's `CmdSetView::post`. After that window's world draws, `run_render` pushes one `CmdPostProcess`. An active stack still pushes the view and the marker when the culled list is empty, so the stack runs on the clear color. A stack on any other entity is ignored. `render_world_viewport` leaves `post` empty, the same way it leaves fog empty.

The world component is `post_stack`: `hdr`, and `effects` as tables of `shader`, `params`, and `sample_depth`.

```toml
post_stack = { hdr = true, effects = [
    { shader = "a0e1b2c3d4f5678901234567890abc10", params = [0.8, 1.1, 0.9, 1.0] }
] }
```

```cpp
world.emplace<engine::render::PostStack>(camera, engine::render::PostStack{
    .hdr = true,
    .effects = {engine::render::PostEffect{.shader = engine::builtin::shader_post_grade, .params = {0.8f, 1.1f, 0.9f, 1.0f}}},
});
```

The game does not pick the float format. `scene_color_format` is `R11G11B10` for an opaque HDR window and `Rgba16F` for a transparent one (the window clear alpha is 0). Where the backend cannot render to that format it logs once and runs the stack in `Rgba8` (`hdr` stays set on the component; the backend's effective flag is off, so the last user pass writes the window and the tonemap is skipped). An empty effect list still copies SceneA to the window. OpenGL blits that color buffer. Vulkan draws `shader_post_grade` at white (`PostChain::ldr_copy`), because the swapchain image is not a transfer destination. The tonemap is not that copy: Reinhard would darken an image already in 0..1.

`post_steps` is the ping-pong both backends follow. The world writes `SceneA`. Each effect reads the previous image and writes the other scene image. With `hdr` off, the last effect writes the window. With `hdr` on, user passes stay on the scene images and the tonemap writes the window. Bloom is not one sample of `uScene`: the backend copies the source, builds half / quarter / eighth (`uParams.z` modes 0, 1, then 2), and adds those levels. `uParams.x` is the threshold and `uParams.y` the intensity.

A post shader's vertex stage is the backend's fullscreen triangle. The fragment stage reads `uScene` (binding 1), `uParams`, `uResolution`, and `uTime` (`CmdSetView::time`). `(0, 0)` is the top-left of the window on both backends. GLSL samples `vec2(vUV.x, 1.0 - vUV.y)` because a GL texture is bottom-up; Slang samples `vUV` directly. `uDepth` (binding 2) is bound only when the pass has `sample_depth`. Depth is a texture only then; otherwise the scene depth is a renderbuffer (OpenGL) or an unsampled depth image (Vulkan). A later pass that writes SceneA uses a framebuffer with that color and no depth attachment, so sampling `uDepth` is not a read of the framebuffer it is drawing to. The world pass still draws into the framebuffer that owns the depth.

UI stays after `CmdPostProcess`, on the window. Particles are world draws, so they are inputs to the stack. A `CmdSetView` with `viewport` set ignores `post` and draws into the window. `frame_targets` records that schedule for tests: world draws after a full-window active view are `Scene` until the marker, then `Window`.

`read_pixels` still reads the window after `render`, so a screenshot stores the tonemapped image with UI on top.

Builtin shaders: `shader_post_grade` (`…abc10`) multiplies rgb by `uParams.rgb` and copies alpha; `shader_post_tonemap` (`…abc11`) is Reinhard `c / (c + 1)`; `shader_post_bloom` (`…abc12`); `shader_post_depth` (`…abc13`) tints by the depth sample.

## Sprites, animation, particles, curves

`Sprite` (`include/engine/render/sprite.h`) is a textured quad: color, layer, order, flip, tiling, offset, pixel size, pixels per unit, pivot, optional mesh, material, and override.

`SpriteAnimator` plays a `SpriteAnimationClip` and writes the current frame onto a sibling `Sprite` (`run_sprite_animations`, frame `delta_time`) through `Sprite::apply_frame`; `Sprite::from_frame` builds a sprite from a frame, and `set_param` / `clear_overrides` on `Sprite` and `Renderable` manage the per-instance `MaterialOverride` (see [Materials and Sort](../features/Materials%20and%20Sort.md)). Clips load from `importer = "animation"` TOML (`fps`, `loop`, frame list).

`ParticleDraw` on `ParticleEmitter` is `Plane` (default), `Billboard`, `Stretched`, or `Mesh`. `Plane` expands the quad in world XY. `Billboard` faces the camera. `Stretched` faces the camera and points local +Y along the in-plane part of velocity, using `Billboard` when that part is near zero (`particle_card_offset`). `Mesh` instances `emitter.mesh` in one draw (`uModel` identity, `uInstanced` 1) through the material shader. `mesh_scale` (`vec3`, default 1) is multiplied by `size_curve` when that curve has keys, and left unchanged when it does not. `size` and `size_curve_xy` do not scale a mesh. `texture_layer` (default 0) is copied onto the particle at spawn; array shaders sample it and a `sampler2D` ignores it. Orientation is identity. Depth test, depth write, and cull stay off for `Plane`. `Billboard` and `Stretched` use the material's depth test, depth write, and cull, and depth-sort when that material depth-tests: a transparent batch sorts its instances back to front and keys from its farthest particle. An opaque depth-writing card joins the opaque queue and keeps spawn order. `Mesh` is depth-tested the same way (`depth_tested` true): a transparent depth-tested batch sorts `mesh_instances`, and an opaque depth-writing batch keeps spawn order. The mesh path applies the material `RenderState` in full, including `alpha_cutoff`. A shader that does not declare `uInstanced` and instance locations 7, 8, and 9 skips that emitter and is logged once per process. A null shader still collects. The texture material `run_render` builds stays depth-off. 3D collision is in. Emitter culling is in (wind-332). `collect_world_draws` skips an emitter no bound window's frustum hits, after the empty-list, missing-mesh, and missing-instance-input skips, so `run_render` and `render_world_viewport` share one site. The box is the union of the live particles at the world position the command writes. `Plane` grows by half `|size|` on XY and 0 on Z (a rotated quad can stick out). `Billboard` and `Stretched` use a radius of half the longer side on every axis, which does not cover the corner. `Mesh` is `transform_aabb` of `IMesh::bounds()` under `translate(position) * scale(mesh_scale * size factor)`, so an off-center box and a negative scale are included. `frustum_cull` defaults to true and is not serialized. False always draws. An empty frustum span does not cull. Today `ParticleEmitter` simulates in `run_particles` with frame `delta_time`. `run_render` pushes one `CmdDrawParticles` when `particles` is not empty and the box is in view. An empty list pushes nothing and does not report. When the list is not empty, a mesh or material that is still null is fatal whether or not the emitter is on screen, and that entity is skipped.

Shapes are point, box, circle, cone, sphere, and disc. Sphere is uniform in the ellipsoid of `shape_scale` (semi-axes, the volume, not the surface). Disc is circle's ellipse (`shape_scale.x` and `shape_scale.y`) in the plane of `direction`, and `(0, 1, 0)` when that vector is near zero; `shape_scale.z` is unused. Circle still writes `z = 0`. Space is world or local. Playback is `play`, `pause`, `stop`, and `burst` (`include/engine/render/particles.h`).

Mesh and material, in `run_render`:

- `mesh`, or `builtin::mesh_quad` when a card's `mesh` is null and that asset loads. `Mesh` does not use that fallback: a null `emitter.mesh` is the missing-mesh fatal, and the backend draws nothing.
- An explicit `material` is used as-is. `CmdDrawParticles.blend` becomes that material's blend.
- A null `material` with a texture reuses the `ctx<SpriteMaterialCache>` entry for that texture pointer (the same cache sprites use), or, when `builtin::shader_unlit` loaded, builds `Material(builtin::shader_unlit, texture, white, {.blend = emitter.blend})` and stores it.
- A null `material` and a null texture use `builtin::material_unlit` when it loads.
- In both null-material cases `CmdDrawParticles.blend` stays `emitter.blend` (default `Alpha`). The OpenGL backend applies `cmd.blend` for particles (`src/render/opengl/opengl_backend.cpp`).

When `collision_enabled` is set, `run_particles` copies non-trigger colliders into `ParticleCollider2D`s and `ParticleCollider3D`s:

- `BoxCollider2D` becomes `Shape::Box`, with `box.size`, axis-aligned. Box yaw is not copied. The test uses X and Y only, so the wall is infinite in Z.
- `CircleCollider2D` becomes `Shape::Circle`, with `circle.radius`.
- `PolygonCollider2D` is not copied.
- `BoxCollider3D` becomes `ParticleCollider3D::Shape::Box`, with `box.size` and the world quaternion from `Transform::rotation` (the parent chain, scale left out). The segment is tested in the box's local space.
- `SphereCollider3D` becomes `Shape::Sphere`, with `sphere.radius`. Rotation is ignored.
- `CapsuleCollider3D` becomes `Shape::Capsule`, with `radius` and `height` (the distance between the sphere centers, along local Y). The same world quaternion orients it.
- Triggers are not copied. `Transform.scale` is not applied.

`update_emitter` takes the 3D span beside the 2D span. Bounce, friction, `lifetime_loss`, and `kill_on_collision` are shared. The 2D box test is not extended to Z. Shape tests run inside the integrate job. `collision_grid` with a `ParticleGrid` context (`try_ctx`, a function pointer and a `const void*` the game owns) runs `sweep_aabb` on the caller after that job joins. The swept box is `collision_radius` around the particle (`Aabb` min/max). Radius 0 on `Mesh` uses half a cell, so the full extent is one cell. No context, or a null `is_solid`, does nothing. The predicate is built for the sweep and is not stored across frames. A job does not call it.

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

Shading language is **Slang** (`external/slang`, `tools/shader_cook/slang_compiler.cpp`). The compiler is not in the engine library. `.shader` files in the source tree are XML with Slang source (`<shader lang="slang"><![CDATA[...]]></shader>` or `<slang>`). `asset_codegen` cooks each one into a `WSHD` blob (`encode_shader_blob`): SPIR-V for Vulkan and GLSL 330 core for OpenGL. Optional `<glsl><vertex>...</vertex><fragment>...</fragment></glsl>` overrides the GLSL source while keeping Slang's SPIR-V; legacy `<xml><vertex>...</vertex><fragment>...</fragment></xml>` raw GLSL shaders remain supported. The staged file keeps the `.shader` name. `load_gpu` decodes the blob and does not parse XML. A host cook without Slang keeps the `<glsl>` text and writes empty SPIR-V. `asset_codegen` fails the build with `CodegenErrorKind::Shader` when the source does not cook. Desktop OpenGL target is `ShaderTarget::Glsl330Core`; GLES (web and Android) uses `Glsl300Es` (`adapt_glsl` / `adapt_shader` in `include/engine/render/shader_adapt.h`). The Vulkan particle program and the Vulkan UI program are baked SPIR-V (`particle_spirv.inc`, `ui_spirv.inc`) generated from `tools/shader_cook/shaders/`.

## OpenGL (`ENGINE_WITH_WINDOW`)

These sources are not in the headless library. CMake adds them only when the window flag is on.

| Piece | File |
| --- | --- |
| Factory | `src/render/opengl/opengl_factory.cpp` |
| Mesh, shader, texture | `opengl_mesh.cpp`, `opengl_shader.cpp`, `opengl_texture.cpp` |
| Execute mesh and particle draws | `opengl_backend.cpp` |
| Scene targets and the post stack | `opengl_post.cpp` |
| One VAO per GL context | `context_vao.cpp`, bookkeeping in `src/render/context_objects.cpp` |
| Canvas, font and image upload | `opengl_canvas.cpp` |
| UI device (`IUiDevice`) and its shader | `opengl_ui_device.cpp`, `opengl_ui_shader.h` |
| SDL window and GL context | `window_system.cpp` |
| Several windows | `window_manager.cpp` |
| Presentation | `sdl_gl_presentation.cpp` |

Desktop links glad and OpenGL 3.3. `ENGINE_WITH_GLES` links SDL without glad (Android also links GLESv3, EGL, `android`, and `log`).

Sampler state on upload comes from the catalog filter, wrap, and mipmaps (`opengl_texture.cpp`, [Mipmaps](#mipmaps)). A texture array is a `GL_TEXTURE_2D_ARRAY` uploaded with `glTexImage3D`, each layer flipped like a 2D texture.

View state (`opengl_backend.cpp`). `OpenGLRenderBackend` keeps one `CmdSetView` and records the context's full-window `GL_VIEWPORT` (`full_viewport_[4]`) at the start of `execute`. `execute` resets `view_` to a default `CmdSetView` (identity, no lights, full viewport) before the first command, a `CmdSetView` replaces it, and each mesh and particle draw sets `uView`, `uProjection`, `uCameraPosition`, and the light uniforms from it ([Lit shader](#lit-shader)). When `CmdSetView::viewport` is set (`Rect` in top-left window coordinates), `set_view` converts it to bottom-left OpenGL coordinates (`gl_y = full_viewport_[3] - (y + h)`), sets `glViewport` and `glScissor`, and enables `GL_SCISSOR_TEST`; when `viewport` is `std::nullopt`, it restores `full_viewport_` and disables `GL_SCISSOR_TEST`. When `CmdSetView::clear_color` is set, `set_view` enables depth writes, clears `GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT` to that color (clipped to the active scissor rect when `viewport` is set), and restores `glClearColor(0, 0, 0, 1)`. At the end of `execute`, the backend restores `full_viewport_` and disables `GL_SCISSOR_TEST`.

Albedo. A mesh draw binds the material's slot-0 texture on unit 0 or, when the material has none (or it is not valid), a 1x1 white RGBA texture the backend makes on first use (`white_texture`, shared by every context). The other target of unit 0 gets white too: `GL_TEXTURE_2D_ARRAY` for a 2D albedo, `GL_TEXTURE_2D` for an array ([Texture arrays](#texture-arrays)). So an untextured material draws as its color, under any shader. The particle draw has always done this; before wind-238 an untextured mesh sampled whatever unit 0 still held (the previous draw's texture, or nothing after the UI flush). A `MaterialOverride` albedo still replaces it for that draw.

Pipeline state of world draws (`opengl_backend.cpp`). Each `CmdDrawMesh` sets all of it from `material->render_state()`, so nothing leaks from the previous draw:

- Blend: `Opaque` disables `GL_BLEND`; `Alpha` is `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`; `Additive` is `SRC_ALPHA, ONE`.
- Depth: test and write both off disables `GL_DEPTH_TEST`. Otherwise `GL_DEPTH_TEST` is on with `glDepthFunc(GL_LEQUAL)` (an equal depth passes, so a second pass over the same surface draws), or `GL_ALWAYS` when only write is on (GL writes no depth while the test is disabled). `glDepthMask` follows `depth_write`.
- Cull: `None` disables `GL_CULL_FACE`; `Back` / `Front` enable it with `glFrontFace(GL_CCW)` and that `glCullFace`.
- Cutout: `uAlphaCutoff` ([Cutout](#cutout)).
- `CmdDrawParticles` for a card uses `cmd.blend`. `Plane` forces depth test off, depth write off, and no cull. `Billboard` and `Stretched` copy `depth_test`, `depth_write`, and `cull` from the material. Card particles ignore `alpha_cutoff`. `Mesh` applies the material `RenderState` in full, including blend, depth, cull, and `alpha_cutoff`, on the mesh pipeline.

After the last command `OpenGLRenderBackend::execute` disables `GL_DEPTH_TEST` and `GL_CULL_FACE` and sets `glDepthMask(GL_TRUE)`. The UI flush that follows sets its own state anyway (no depth, stencil, cull). The mask matters for the next frame: `glClear` honours it, so a false mask would keep last frame's depth. `OpenGLCanvas::render` also sets `glDepthMask(GL_TRUE)` right before its `glClear(COLOR | STENCIL | DEPTH)`. State is per context: each window's canvas runs both in its own context, so every window starts its frame with a cleared depth buffer. The default framebuffer has a 24-bit depth buffer (`SDL_GL_DEPTH_SIZE`, `window_system.cpp`).

Each canvas's UI painter is a `ui::UiRenderer` (`src/ui/render/ui_renderer.h`): the backend-neutral `BatchingPainter` over the one `ui::PaintResources` the `OpenGLRenderBackend` keeps for every window (`ui_resources()`), with that canvas's own `OpenGLUiDevice`. `end_frame` catches the device's textures up with the shared atlas pages (`UiTextureSync`: a new page whole, a changed one its dirty rect, everything again after `PaintResources::generation` changes), uploads the frame's vertices and indices into two orphaned stream buffers, and issues one `glDrawElements` per batch, so the UI profiler's `draw_calls` is the batch count. The device is one program (GLSL 330 core, rewritten to 300 es by `adapt_glsl` under `ENGINE_WITH_GLES`) that implements `src/ui/render/fragment.h`, its own textures (R8 glyph and mask pages, RGBA8 ramp and image pages; bilinear, clamp, no mipmaps), and a VAO through `ContextVao`. It sets its whole pipeline when it draws and leaves what the rest of the backend expects: blend on with the premultiplied function, depth, stencil, cull, and scissor off, program, VAO, array buffer, and texture units 0 to 3 unbound with unit 0 active, unpack alignment 4. `ENGINE_GL_CHECKS` also checks for GL errors after the UI draw ("the UI draw"). `reset_ui_painter` (editor Stop) drops the shared images (`PaintResources::drop_images`), and the next frame registers and uploads the ones still referenced; fonts stay registered, being shared by windows that may be measuring text. The UI has no other painter. `fill_path` and `stroke_path` are coverage masks in that atlas, a stroke keyed by its device-pixel width as well as the path.

Every window has its own GL context. Textures, buffers, and programs are shared with the primary's context; vertex array objects are not. A VAO made in one context binds nothing, or another VAO, in the next, and the draw does nothing without an error. So `OpenGLMesh` and the particle draw keep their buffers once and make a VAO in each context the first time they draw there (`ContextVao`). The key of a context comes from `ContextObjects`, which `OpenGLCanvas` fills when it creates and destroys a context. A key is never reused, so a VAO cached for a destroyed context never binds in a new one whose driver handle is the same. A VAO released while another context is current is deleted the next time its own context renders. A destroyed context drops its VAOs with it.

GL errors are silent unless someone asks. With `ENGINE_GL_CHECKS` (Debug, RelWithDebInfo, the editor, and the Development player; not Shipping) `OpenGLRenderBackend::execute` drains `glGetError` before the first command and after each command and logs `GL_INVALID_OPERATION (0x0502) after CmdDrawParticles` once per error and command type (`GlErrorLog`, `src/render/gl_error_log.cpp`), so a draw that fails every frame writes one line, not one a frame. `OpenGLShader` logs the driver's info log when a shader fails to compile or a program fails to link, in every build.

`WindowSystem::set_icon` builds an `SDL_Surface` with `make_icon_surface` and calls `SDL_SetWindowIcon`. A null window or a short RGBA buffer yields a null surface. `tests/window_icon_test.cpp` covers the byte layout without `SDL_Init`.

## Vulkan (`ENGINE_WITH_WINDOW`)

`src/render/vulkan/` provides the Vulkan backend alongside OpenGL when `ENGINE_WITH_WINDOW` is enabled (excluding Emscripten):

| Piece | File |
| --- | --- |
| Dynamic function loader (`VK_NO_PROTOTYPES` via `SDL_Vulkan_GetVkGetInstanceProcAddr`) | `src/render/vulkan/vulkan_functions.cpp` |
| Shared `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, and command pool | `src/render/vulkan/vulkan_context.cpp` |
| Factory (`IMesh`, SPIR-V `IShader` modules, `ITexture`, `IDynamicTexture`) | `src/render/vulkan/vulkan_factory.cpp` |
| GPU resources (`VulkanMesh`, `VulkanShader`, `VulkanTexture`) | `vulkan_mesh.cpp`, `vulkan_shader.cpp`, `vulkan_texture.cpp` |
| Buffers held until no frame in flight reads them (`RetireQueue`) | `src/render/vulkan/vulkan_retire_queue.h` |
| SPIR-V reflection (`OpEntryPoint`, UBO uniform layout, sampler binding) | `src/render/vulkan/vulkan_spirv.cpp` |
| Render backend (`IRenderBackend`, graphics pipelines, UBO/descriptor sets, `CmdSetView`, `CmdDrawMesh`, `CmdDrawParticles`, `CmdPostProcess`) | `src/render/vulkan/vulkan_backend.cpp` |
| Scene images, the pass split, and bloom targets | `src/render/vulkan/vulkan_post.cpp` |
| UI device (`IUiDevice`) and its baked SPIR-V | `vulkan_ui_device.cpp`, `ui_spirv.inc` |
| Per-window `VkSurfaceKHR`, `VkSwapchainKHR`, depth attachment, render pass, framebuffers, sync primitives, and staging-buffer `read_pixels()` | `src/render/vulkan/vulkan_canvas.cpp` |
| SDL presentation wiring (`make_sdl_vk_presentation`) | `src/render/vulkan/sdl_vk_presentation.cpp` |

Retired buffers (`src/render/vulkan/vulkan_retire_queue.h`, held by `VulkanContext`). A buffer a mesh replaced or dropped may still be read by up to `kMaxFramesInFlight` submitted frames, or by the frame being recorded. `VulkanContext::retire_buffer` tags it with the serial of the next submission. Each `VulkanCanvas` records `VulkanContext::note_submitted()` for a frame slot after its `vkQueueSubmit`, and passes that serial to `note_completed` after waiting on the slot's fence, which destroys everything tagged at or below it. Every canvas submits to the one graphics queue, and a fence signalled by a submit covers all earlier submits on that queue, so one completed serial is enough even with several windows. `release_retired` destroys everything after a `vkDeviceWaitIdle` (canvas shutdown, swapchain recreation, `VulkanContext::shutdown`). `VulkanTexture` does not use it yet: a dynamic texture whose size changes destroys its image at once, which a frame in flight may still sample.

Vulkan mesh pipelines declare all five `MeshVertex` attributes (color is `R32G32B32A32_SFLOAT` at location 3, layer `R32_SFLOAT` at location 4). `VulkanTexture` holds a 2D texture or an array (`arrayLayers`, a `2D_ARRAY` view) with its mip chain ([Mipmaps](#mipmaps)); the albedo it binds matches the shader's sampler ([Texture arrays](#texture-arrays)). The pipeline cache key is the shader modules, the render state with `alpha_cutoff` cleared (the cutoff is a uniform, so one pipeline serves every value), the render pass, and the particle flag. `uAlphaCutoff` and the fog uniforms go into the draw's uniform block like the others.

`EngineRuntime` selects `make_sdl_vk_presentation()` when built with `-DENGINE_WITH_VULKAN=ON` or when `WIND_RENDER_BACKEND=vulkan` is set in the environment (`WIND_RENDER_BACKEND=opengl` forces OpenGL). The engine enables no implicit layer. `VK_LAYER_KHRONOS_validation` is requested only when `WIND_VK_VALIDATION=1` and that layer is installed. A loader-injected implicit layer that fails `vkCreateInstance` or `SDL_Vulkan_CreateSurface` still fails startup: `WindowSystem::create` logs `SDL_GetError` when `SDL_CreateWindow` returns null, and `VulkanCanvas::init` / `VulkanContext` log the `VkResult` name and value for each failed Vulkan call (`game.log`, then the `WindowCreate` dialog). `VK_LOADER_LAYERS_DISABLE=*` is the way to start that same process with the implicit layers out. There is no fallback to OpenGL after the Vulkan presentation is chosen: `EngineServices` already holds that factory.

Scene uniforms are written column-major unless SPIR-V reflection says the member is row-major (`write_mat4` transposes only then). This Slang decorates `uModel`, `uView`, and `uProjection` `RowMajor` and emits no `OpTranspose`, even with `SessionDesc::defaultMatrixLayoutMode` set to column-major, so the transpose is the matching upload: without it a translation in the last column becomes w. A matrix wrapped in a one-member struct keeps the inner member's `RowMajor` / `ColMajor` and matrix stride. `uProjection` is `vulkan_projection` of the camera matrix, so an orthographic camera with a negative `near_clip` (clip z in [-w, w] from `glm::ortho`) lands in Vulkan's [0, w]. The viewport's negative height flips Y to match OpenGL; it does not change w. `tests/vulkan_clip_test.cpp` is the 2D ortho fixture with `ortho_size` 450, near -100, far 100, and the camera at z = 10.

## CPU sources (always)

- `src/render/material.cpp` — parse `.mat`
- `src/render/mesh.cpp` — `IMesh::update` (validation before the backend's `replace`), `mesh_bounds`, the `IMesh` bounds
- `src/render/aabb.cpp` — `transform_aabb`
- `src/render/frustum.cpp` — `make_frustum`, `frustum_intersects`, `visible_in_any`
- `src/render/texture_check.cpp` — `texture_array_desc_error`, `mip_level_count`, per-layer row flip, `texture_rects_error` and the rect copy into a bottom-row-first image
- `src/render/lights.cpp` — `collect_scene_lights`, `default_scene_lights`
- `src/render/post_process.cpp` — `prepare_post_stack`, `resolve_post_chain`, `post_steps`, `frame_targets`
- `src/render/material_instance.h` — `IMaterial` for a cooked material
- `src/render/tilemap.cpp`, `src/render/animation.cpp`, `src/render/particles.cpp`, `src/render/shader_adapt.cpp`, `tools/shader_cook/slang_compiler.cpp`
- `src/render/context_objects.cpp` — GL context keys and deferred VAO deletes, without GL calls
- `src/render/gl_error_log.cpp` — GL error names and the once-per-place error log lines

## Public headers

- `include/engine/render/aabb.h`
- `include/engine/render/commands.h`
- `include/engine/render/command_buffer.h`
- `include/engine/render/frustum.h`
- `include/engine/render/directional_light.h`
- `include/engine/render/fog.h`
- `include/engine/render/post_process.h`
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
- `include/engine/render/tilemap.h`
- `include/engine/render/particles.h`
- `include/engine/render/curve.h`
- `include/engine/render/shader_adapt.h`
- `include/engine/render/texture_array.h`

## Tests

`tests/command_buffer_test.cpp`, `tests/lights_test.cpp` (light collection, lights and camera in `CmdSetView`), `tests/fog_test.cpp` (no fog by default, the camera's `Fog` in every view, none in `render_world_viewport`, serialization), `tests/post_process_test.cpp` (prepare, ping-pong, `frame_targets`, absent stack, viewport, bloom without HDR, missing shader, `post_stack` serialization), `tests/sort_test.cpp`, `tests/material_test.cpp`, `tests/render_system_test.cpp` (also culled renderables and sprites push no draw, pixel size and pivot, the union over windows, `frustum_cull`, particle emitters culled including a straddle, `frustum_cull = false`, and a scaled off-center mesh, `render_world_viewport` culling), `tests/frustum_test.cpp` (inside, outside each plane, straddling, touching, orthographic and perspective, degenerate matrices, `transform_aabb`), `tests/sprite_test.cpp`, `tests/animation_test.cpp`, `tests/tilemap_test.cpp` (chunk culling, chunk edits update the chunk's mesh), `tests/mesh_test.cpp` (`IMesh::update` validation and keep-old rule, bounds from create and update, `.mesh` vertex colors), `tests/texture_array_test.cpp` (array desc checks, mip level count, frame import, `.meta` and catalog, `AssetsDb` layers by name, array albedo in a `.mat`, codegen), `tests/vulkan_backend_test.cpp` (window builds: mesh update without a device, `RetireQueue` serials, a texture array without a device, `sampler_arrayed` reflection), `tests/particle_test.cpp`, `tests/shader_adapt_test.cpp`, `tests/opengl_texture_test.cpp` (CPU texture desc, no GL draw), `tests/ui_painter_test.cpp` (fake painter), `tests/window_icon_test.cpp`, `tests/context_objects_test.cpp` (context keys and deferred VAO deletes, no GL), `tests/gl_error_log_test.cpp`, `tests/ui_renderer_test.cpp` (UI renderer against a fake device: texture catch-up per device, draws per batch).

GPU pixels stay out of `engine_tests`. See [Boundaries](../architecture/Boundaries.md).

## See also

- [Materials and Sort](../features/Materials%20and%20Sort.md)
- [ECS](ECS.md)
