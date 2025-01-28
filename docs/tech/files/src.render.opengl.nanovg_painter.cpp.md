---
tags: [file, render]
aliases: [src/render/opengl/nanovg_painter.cpp, nanovg_painter.cpp]
---

# `src/render/opengl/nanovg_painter.cpp`

Module: [[modules/Render]]

NanoVG `IUiPainter`. Fonts in memory. `scissor` is `nvgIntersectScissor`. `apply_view` is `T(origin) S(zoom) T(pan) T(-origin)` so pan is in pre-scale units (`displayed = origin + zoom * (layout - origin + pan)`). `image()` draws via `nvgImagePattern` against an `AssetId`-keyed image map (`add_image` uploads with `nvgCreateImageRGBA`, called from Init for `ImporterKind::Texture`/`UiImage` catalog entries).

See [[features/OpenGL]], [[features/UI Markup]].

Repo path: `src/render/opengl/nanovg_painter.cpp`
