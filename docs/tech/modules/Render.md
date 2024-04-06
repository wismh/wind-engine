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
- [[src.render.opengl.window_system.cpp]] — SDL window + GL context.

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

[[tests.command_buffer_test.cpp]] · [[tests.sort_test.cpp]] · [[tests.material_test.cpp]] · [[tests.render_system_test.cpp]] · [[tests.ui_painter_test.cpp]] (fake painter)

GPU pixels are out of `engine_tests` (SDD §12.3).

## See also

- [[features/Materials and Sort]]
- [[features/OpenGL]]
- [[architecture/Boundaries]]
