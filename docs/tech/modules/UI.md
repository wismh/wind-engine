---
tags: [module]
---

# UI

XML + custom CSS + C++ MVVM. UI is an ECS component (`UiCanvas` + `UiInstance`), not a C++ widget graph and not a `Layout`/`onClick` tree.

## Capabilities

- Tags: Canvas, Stack, Label, Button, Image, ItemsControl, ItemTemplate ([[include.engine.ui.document.h]]).
- `{binding path}` on `text` / `content` / `command` / `source` / `items_source`, interned to a `BindingId` ([[include.engine.ui.binding_id.h]]) at parse time — not a runtime string lookup.
- CSS: element / class / id / `E.c`, descendant `A B`, child `A > B` (no `+`/`~`, no `,` grouping); `:hover` `:pressed` `:disabled`; units px/`%`/`em` + `calc(+ - * /)`; `@media (min-width|min-height: N)`; `@keyframes` (opacity only, via `animation-name`/`animation-duration`).
- Applied style: color, background, background-image, opacity, visibility, width, height, min-width, min-height, gap, flex-direction, padding, margin, justify-content, align-items, text-align, border-*, font-size, font-family (AssetId hex or `default`), animation-name, animation-duration, z-index, position, top/right/bottom/left, transform.
- `z-index` (sibling-local stacking order), `position: relative|absolute` (against the nearest positioned ancestor or the canvas root), `transform: rotate() scale()` (paint-time only, about the element's own center) — see [[include.engine.ui.document.h]] `PositionMode` and §8.3 of the SDD for the full model and non-goals (no MVVM binding, no `@keyframes`, AABB-only hit-test for rotated elements).
- `ICommand` / `RelayCommand` — only UI → game path.
- Hit-test canvases by `order` (front first); within a canvas, siblings resolve topmost-first by the same `z-index` stacking order paint uses ([[src.ui.document.cpp]] `hit_test`); only a hit **Button** sets `MouseConsumed` (Label/Stack/Canvas/Image do not consume), and `:hover`/`:pressed` follow that same single topmost hit, not every geometrically-overlapping Button. `FillWindow` copies `WindowSize` into `rect`. `ScaleWithScreenSize` (Unity `PanelSettings`-style) keeps layout/paint/hit-test in fixed `reference_size` design units and lets the engine derive one uniform letterbox `scale` + `offset` from `rect` — pixel-perfect scaling for a fixed pixel-art canvas at any window size, see [[include.engine.ui.canvas.h]] `canvas_layout_space`.

## How it is implemented

- [[src.ui.xml_parser.cpp]] — tinyxml2; interns `{binding}` paths to `BindingId` via [[src.ui.bind_scan.h]].
- [[src.ui.css_parser.cpp]] — combinators, units, `calc()`, `@media`, `@keyframes`; unknown property → warning string, not fatal.
- [[src.ui.document.cpp]] — real content-box layout (padding/margin/gap/width/height/min-* affect sizing; Stack packs by used size + `justify-content`/`align-items`), bindings by `BindingId`; `position: absolute` children are split out of the packing/cursor loop and resolved against a threaded `containing_block` instead; `child_stacking_order()` (z-index sort) and `hit_test()`/`hit_bounds()` (topmost-first, AABB for rotated/scaled elements) are exported here and shared by paint.cpp and canvas.cpp.
- [[src.ui.paint.cpp]] — cascade (incl. `@media`), interaction flags (topmost-hit-only via `hit_test()`), text position from justify/align/text-align, opacity keyframe sampling, `z-index` sibling sort before recursing into children, `IUiPainter::apply_transform` for non-identity rotation/scale, calls `IUiPainter`.
- [[src.ui.canvas.cpp]] — begin_frame, handle_pointer (widget-level `MouseConsumed` via the shared `hit_test`), apply_canvas_fit, canvas_layout_space.
- [[src.ui.view_model.cpp]] — `property_`/`command_` maps keyed by `BindingId`, not name.
- [[src.ui.painter.h]] — private painter interface.
- [[src.ui.splash.h]] — `build_splash_document(config, image_size)` turns `IGame::splash_screen()`'s
  config into an in-memory XML `Image` (fixed `position:absolute; left/top:10%; width/height:80%`
  box, not `100%` — stretching would ignore the image's aspect ratio) + a 4-stop `@keyframes`
  opacity CSS, plus a `reference_size = image_size / 0.8` for the canvas, via the same
  `parse_xml`/`parse_css` every other document goes through (no hand-built `Element`/`Keyframes`
  structs); spawned as a `UiCanvas{fit=ScaleWithScreenSize, reference_size, order=1000}` +
  `UiInstance` entity in `EngineRuntime::begin_loop()` (`src/core/engine_runtime.cpp`, not `Host`
  — see SDD §20.3), on top of every other canvas. `ScaleWithScreenSize`'s existing contain-fit
  letterboxes that canvas into the real window preserving the image's aspect ratio, so the fixed
  80% image box lands with a minimum 10% margin on every edge; `image_size` comes from a small
  `AssetId → glm::vec2` map `EngineRuntime` fills in as `add_image()` receives each `TextureDesc`.
  The root `<Canvas>` also carries its own rule, `background: #000000` with **no**
  `animation-name` — constant, not faded — because the game underneath is already running
  (`on_start()`/`Schedule::Fixed`/`Frame` are never gated) and would otherwise show through
  during the image's own fade-in/out. `EngineRuntime` tracks a separate `splash_elapsed` timer in
  `tick_loop()` against `SplashDocument::total_duration` and `world.destroy()`s the whole entity
  once it's past, since that constant backdrop has no animation of its own to make it disappear.
- [[src.resources.codegen.cpp]] — for `importer = "ui"` XML, emits a binder struct (e.g. `assets::ui::Hud::bind(vm)`) with one `constexpr BindingId` per `{binding}` path; fails the build on an intern collision. `ViewModel` subclasses and `Bindable<T>` members stay hand-written.

NanoVG implementation: [[src.render.opengl.nanovg_painter.cpp]] — `Image` and CSS `background-image` both paint via `IUiPainter::image(AssetId, Rect)`, backed by an `AssetId`-keyed NanoVG image map populated at Init from `ImporterKind::Texture`/`UiImage` catalog entries.

Font faces: builtin UI font plus every catalog `ImporterKind::Font` registered in Init ([[features/Init and Loop]]).

## Public headers

- [[include.engine.ui.document.h]]
- [[include.engine.ui.stylesheet.h]]
- [[include.engine.ui.canvas.h]]
- [[include.engine.ui.view_model.h]]
- [[include.engine.ui.bindable.h]]
- [[include.engine.ui.binding_id.h]]
- [[include.engine.ui.command.h]]

## Tests

[[tests.ui_xml_test.cpp]] · [[tests.ui_css_test.cpp]] · [[tests.mvvm_test.cpp]] · [[tests.ui_painter_test.cpp]] · [[tests.assets_test.cpp]] (codegen) · [[tests.splash_test.cpp]]

## See also

- [[features/UI Markup]]
- [[features/UI Input]]
- [[modules/ECS]]
- [[build/Asset Codegen]]
