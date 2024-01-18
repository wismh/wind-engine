---
tags: [feature]
---

# UI Markup

## Parse

[[src.ui.xml_parser.cpp]]: tinyxml2, known tags only. `command` must be `{binding}`. `source` is 32-hex or binding, never a filename. `{binding path}` attributes (`text`/`content`/`command`/`source`/`items_source`) are interned to a `BindingId` at parse time ([[include.engine.ui.binding_id.h]]), not stored as strings.

[[src.ui.css_parser.cpp]]: selectors `E`, `.c`, `#id`, `E.c`, descendant `A B`, child `A > B` (no `+`/`~`, no `,` grouping), optional `:hover|:pressed|:disabled`. Known properties listed in parser; others warn. Units: px/`%`/`em`, `calc(+ - * /)`. `@media (min-width|min-height: N)` and `@keyframes` (opacity only) — see [[modules/UI]].

## Bind

[[src.ui.document.cpp]] `apply_bindings`: resolve `BindingId` property/command bindings against a `ViewModel` (`property_/command_` maps keyed by `BindingId`, not name). ItemsControl clones ItemTemplate per list item.

`asset_codegen` scans `importer = "ui"` XML (`ui::scan_bind_tree`, [[src.ui.bind_scan.h]]) and emits a binder struct per document — e.g. `assets::ui::Hud` with `static constexpr BindingId title = intern("title")` per path and a `template<typename T> static void bind(T& vm)` that calls `vm.property(title, vm.title)` / `vm.command(...)`. Two paths interning to the same `BindingId` fails codegen. Games write the `ViewModel` subclass by hand (`Bindable<T>` members) and call the generated `Hud::bind(*this)` — codegen never generates `ViewModel` classes or `Bindable<T>` fields. See [[src.resources.codegen.cpp]], [[build/Asset Codegen]].

`run_bind` ([[src.ecs.systems.cpp]]) rebuilds `UiInstance` from the document when it or its data context changes, reapplies bindings each frame, and lazily `try_get<Stylesheet>` for the merged sheet (primary + `extra_stylesheets`).

## Layout

`src/ui/document.cpp` resolves a real content-box model: `padding`/`margin`/`gap`/`width`/`height`/`min-width`/`min-height` (px/%/em/`calc()`, percent against the parent content box) all affect layout. Stack packs children along the main axis by **actual used size** (explicit size, else intrinsic "hug" size — text metrics for Label/Button, `kDefaultImageSize` for Image — clamped up by `min-*`), plus `margin` and `gap`, then applies `justify-content` (main axis) and `align-items` (cross axis). `text-align` positions glyphs independently of `justify-content`/`align-items`.

Canvas/Button/Label children (non-stack) still overlay the same content rect, but each child keeps its own box-resolved size within it.

## Paint

[[src.ui.paint.cpp]]: specificity cascade (element < class < id < pseudo; later same-specificity rule wins), `@media` re-evaluated against the live window size, `:hover` from pointer vs `layout_rect`. Label/Button text: content rect after padding; `justify-content` → x; `align-items` → y; painter align matches ([[src.render.opengl.nanovg_painter.cpp]]). `background-image` and `Image`'s `source` both paint through `IUiPainter::image(AssetId, Rect)`, keyed by the same NanoVG image map. `animation-name`/`animation-duration` drive `opacity` between `@keyframes` stops using real `delta_time`.

`run_ui_render` sorts canvases by `order` (low first = behind) and pushes `CmdDrawUI`.

## Files

- [[include.engine.ui.document.h]]
- [[include.engine.ui.binding_id.h]]
- [[src.ui.xml_parser.cpp]]
- [[src.ui.css_parser.cpp]]
- [[src.ui.document.cpp]]
- [[src.ui.paint.cpp]]
- [[include.engine.ui.view_model.h]]
- [[src.resources.codegen.cpp]]
- [[tests.ui_xml_test.cpp]]
- [[tests.ui_css_test.cpp]]
- [[tests.ui_painter_test.cpp]]
- [[tests.assets_test.cpp]]

## See also

- [[modules/UI]]
- [[features/UI Input]]
- [[build/Asset Codegen]]
