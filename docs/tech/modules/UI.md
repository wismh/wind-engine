---
tags: [module]
---

# UI

XML + custom CSS + C++ MVVM. UI is an ECS component (`UiCanvas` + `UiInstance`), not a C++ widget graph and not ping-pong `Layout`/`onClick`.

## Capabilities

- Tags: Canvas, Stack, Label, Button, Image, ItemsControl, ItemTemplate ([[include.engine.ui.document.h]]).
- `{binding path}` on `text` / `content` / `command` / `source` / `items_source`, interned to a `BindingId` ([[include.engine.ui.binding_id.h]]) at parse time — not a runtime string lookup.
- CSS: element / class / id / `E.c`, descendant `A B`, child `A > B` (no `+`/`~`, no `,` grouping); `:hover` `:pressed` `:disabled`; units px/`%`/`em` + `calc(+ - * /)`; `@media (min-width|min-height: N)`; `@keyframes` (opacity only, via `animation-name`/`animation-duration`).
- Applied style: color, background, background-image, opacity, visibility, width, height, min-width, min-height, gap, flex-direction, padding, margin, justify-content, align-items, text-align, border-*, font-size, font-family (AssetId hex or `default`), animation-name, animation-duration.
- `ICommand` / `RelayCommand` — only UI → game path.
- Hit-test canvases by `order` (front first); only a hit **Button** sets `MouseConsumed` (Label/Stack/Canvas/Image do not consume). `FillWindow` copies `WindowSize` into `rect`.

## How it is implemented

- [[src.ui.xml_parser.cpp]] — tinyxml2; interns `{binding}` paths to `BindingId` via [[src.ui.bind_scan.h]].
- [[src.ui.css_parser.cpp]] — combinators, units, `calc()`, `@media`, `@keyframes`; unknown property → warning string, not fatal.
- [[src.ui.document.cpp]] — real content-box layout (padding/margin/gap/width/height/min-* affect sizing; Stack packs by used size + `justify-content`/`align-items`), bindings by `BindingId`.
- [[src.ui.paint.cpp]] — cascade (incl. `@media`), interaction flags, text position from justify/align/text-align, opacity keyframe sampling, calls `IUiPainter`.
- [[src.ui.canvas.cpp]] — begin_frame, handle_pointer (widget-level `MouseConsumed` via `find_button_at`), apply_fill_window.
- [[src.ui.view_model.cpp]] — `property_`/`command_` maps keyed by `BindingId`, not name.
- [[src.ui.painter.h]] — private painter interface.
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

[[tests.ui_xml_test.cpp]] · [[tests.ui_css_test.cpp]] · [[tests.mvvm_test.cpp]] · [[tests.ui_painter_test.cpp]] · [[tests.assets_test.cpp]] (codegen)

## See also

- [[features/UI Markup]]
- [[features/UI Input]]
- [[modules/ECS]]
- [[build/Asset Codegen]]
