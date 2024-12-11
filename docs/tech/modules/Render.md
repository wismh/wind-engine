---
tags: [module]
---

# Render

Command buffer, materials, sprite sort, OpenGL 3.3 backend, NanoVG UI execute. Games never include glad.

## Capabilities

- `IMaterial`: shader + texture slots + blend + default color; instance tint multiplies ([[include.engine.render.graphics.h]]).
- `CommandBuffer`: `CmdDrawMesh` and `CmdDrawUI` only — no custom GL callback.
- Sort: layer, `order_in_layer`, material pointer, entity index.
- GPU objects created through `IGraphicFactory` (meshes, shaders, textures).
- `ICanvas::draw` — clear, execute, swap.
- Texture sampler from catalog: filter / wrap on upload ([[src.render.opengl.opengl_texture.cpp]]).

## How it is implemented

**CPU (always):**

- [[src.render.material.cpp]] — parse `.mat` TOML → `MaterialInstance`.
- [[src.render.material_instance.h]] — `IMaterial` impl.
- Headers: [[include.engine.render.commands.h]], [[include.engine.render.command_buffer.h]], [[include.engine.render.renderable.h]], [[include.engine.render.material.h]], [[include.engine.render.backend.h]], [[include.engine.render.canvas.h]], [[include.engine.render.graphic_factory.h]].

**OpenGL (`ENGINE_WITH_WINDOW`):**

- Desktop: OpenGL 3.3 Core + glad + NanoVG GL3.

- [[src.render.opengl.opengl_factory.cpp]] — create GL resources.
- [[src.render.opengl.opengl_mesh.cpp]], [[src.render.opengl.opengl_shader.cpp]], [[src.render.opengl.opengl_texture.cpp]].
- [[src.render.opengl.opengl_backend.cpp]] — execute mesh draws.
- [[src.render.opengl.opengl_canvas.cpp]] — Draw + font forward to NanoVG.
- [[src.render.opengl.nanovg_painter.cpp]] — UI painter; `image()` draws (Image source + CSS `background-image`) via a NanoVG image map.
- [[src.render.opengl.window_system.cpp]] — SDL window + GL context. `WindowSystem::set_icon`
  builds an `SDL_Surface` from a `render::TextureDesc` via the free function
  `make_icon_surface()` (no-op / `nullptr` on a null window or an undersized RGBA buffer) and
  calls `SDL_SetWindowIcon`; factored out so the byte layout is unit-testable without
  `SDL_Init(SDL_INIT_VIDEO)` or a real window.

World draw is **not** in this folder: [[src.ecs.systems.cpp]] `run_render` builds commands.

## Public headers

- [[include.engine.render.commands.h]]
- [[include.engine.render.command_buffer.h]]
- [[include.engine.render.renderable.h]]
- [[include.engine.render.material.h]]
- [[include.engine.render.graphics.h]]
- [[include.engine.render.canvas.h]]
- [[include.engine.render.backend.h]]
- [[include.engine.render.graphic_factory.h]]
- [[include.engine.render.shader_adapt.h]]

## Tests

[[tests.command_buffer_test.cpp]] · [[tests.sort_test.cpp]] · [[tests.material_test.cpp]] · [[tests.render_system_test.cpp]] · [[tests.ui_painter_test.cpp]] (fake painter) · [[tests.window_icon_test.cpp]] (`make_icon_surface`, no real window)

GPU pixels are out of `engine_tests` (SDD §12.3).

## See also

- [[features/Materials and Sort]]
- [[features/OpenGL]]
- [[architecture/Boundaries]]
