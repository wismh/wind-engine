---
tags: [file, render]
aliases: [src/render/opengl/nanovg_painter.cpp, nanovg_painter.cpp]
---

# `src/render/opengl/nanovg_painter.cpp`

Module: [[modules/Render]]

NanoVG `IUiPainter`. Fonts in memory. `scissor` is `nvgIntersectScissor`. `apply_view` is `T(origin) S(zoom) T(pan) T(-origin)` so pan is in pre-scale units (`displayed = origin + zoom * (layout - origin + pan)`). `image()` draws via `nvgImagePattern` against an `AssetId`-keyed image map (`add_image` uploads with `nvgCreateImageRGBA`, called from Init for `ImporterKind::Texture`/`UiImage` catalog entries).

`break_lines` overrides `IUiPainter::break_lines` with `nvgTextBreakLines` (rows in chunks of 32, `lineh` from `nvgTextMetrics`) and maps the row pointers back to byte offsets of the text; without a `vg` it falls back to the interface default. Not covered by `engine_tests` (needs a GL context) — the wrapping rules themselves are tested through `break_text_lines`. `measure_text`, `break_lines`, and `font_metrics` reset the transform first: NanoVG divides those results by the current scale, so a CSS `scale(0)` (a window opening) quantizes every advance to zero. `measure_text` returns the horizontal advance, not the ink box, so a trailing space keeps its width.

See [[features/OpenGL]], [[features/UI Markup]].

Repo path: `src/render/opengl/nanovg_painter.cpp`
