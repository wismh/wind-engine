---
tags: [feature]
---

# UI Markup

## Parse

[[src.ui.xml_parser.cpp]]: tinyxml2, known tags only. `command` must be `{binding}`. `source` is 32-hex or binding, never a filename. `{binding path}` attributes (`text`/`content`/`command`/`source`/`items_source`) are interned to a `BindingId` at parse time ([[include.engine.ui.binding_id.h]]), not stored as strings.

[[src.ui.css_parser.cpp]]: selectors `E`, `.c`, `#id`, `E.c`, descendant `A B`, child `A > B` (no `+`/`~`, no `,` grouping), optional `:hover|:pressed|:disabled`. Known properties listed in parser; others warn. Units: px/`%`/`em`, `calc(+ - * /)`. `@media (min-width|min-height: N)` and `@keyframes` (opacity only) — see [[modules/UI]]. `z-index`, `position`/`top`/`right`/`bottom`/`left`, and `transform: rotate() scale()` are known properties too, parsed the same way but affecting layout/paint order rather than sizing — see Layout/Paint below.

## Bind

[[src.ui.document.cpp]] `apply_bindings`: resolve `BindingId` property/command bindings against a `ViewModel` (`property_/command_` maps keyed by `BindingId`, not name). ItemsControl clones ItemTemplate per list item.

`asset_codegen` scans `importer = "ui"` XML (`ui::scan_bind_tree`, [[src.ui.bind_scan.h]]) and emits a binder struct per document — e.g. `assets::ui::Hud` with `static constexpr BindingId title = intern("title")` per path and a `template<typename T> static void bind(T& vm)` that calls `vm.property(title, vm.title)` / `vm.command(...)`. Two paths interning to the same `BindingId` fails codegen. Games write the `ViewModel` subclass by hand (`Bindable<T>` members) and call the generated `Hud::bind(*this)` — codegen never generates `ViewModel` classes or `Bindable<T>` fields. See [[src.resources.codegen.cpp]], [[build/Asset Codegen]].

`run_bind` ([[src.ecs.systems.cpp]]) rebuilds `UiInstance` from the document when it or its data context changes, reapplies bindings each frame, and lazily `try_get<Stylesheet>` for the merged sheet (primary + `extra_stylesheets`).

## Layout

`src/ui/document.cpp` resolves a real content-box model: `padding`/`margin`/`gap`/`width`/`height`/`min-width`/`min-height` (px/%/em/`calc()`, percent against the parent content box) all affect layout. Stack packs children along the main axis by **actual used size** (explicit size, else intrinsic "hug" size — text metrics for Label/Button, `kDefaultImageSize` for Image — clamped up by `min-*`), plus `margin` and `gap`, then applies `justify-content` (main axis) and `align-items` (cross axis). `text-align` positions glyphs independently of `justify-content`/`align-items`.

Canvas/Button/Label children (non-stack) still overlay the same content rect, but each child keeps its own box-resolved size within it.

`position: absolute` children (Stack or non-stack parent alike) are pulled out of that flow/overlay pass entirely and resolved separately against a `containing_block` rect threaded down through `layout_element`/`layout_stack` — the nearest ancestor with `position: relative` or `position: absolute` (whose own box becomes the containing block for its descendants), falling back to the canvas root. `top`/`right`/`bottom`/`left` (px/%/em/`calc()`, resolved against the containing block) place it; when both opposite insets are set with no explicit size on that axis, the box stretches to fill instead of hugging. `position: relative` stays fully in flow and only nudges the element's own already-placed rect by `top`/`left` (or `-bottom`/`-right`) afterward — siblings already packed against its pre-offset size, so it never reflows them. `z-index`/rotation/scale never affect layout at all (see Paint).

## Paint

[[src.ui.paint.cpp]]: specificity cascade (element < class < id < pseudo; later same-specificity rule wins), `@media` re-evaluated against the live window size, `:hover` from a single `hit_test()` call (topmost element under the pointer, not every element whose rect contains it — see [[features/UI Input]]). Label/Button text: content rect after padding; `justify-content` → x; `align-items` → y; painter align matches ([[src.render.opengl.nanovg_painter.cpp]]). `background-image` and `Image`'s `source` both paint through `IUiPainter::image(AssetId, Rect)`, keyed by the same NanoVG image map. `animation-name`/`animation-duration` drive `opacity` between `@keyframes` stops using real `delta_time`.

`run_ui_render` sorts canvases by `order` (low first = behind) and pushes `CmdDrawUI`. Within a canvas, `paint_element` stable-sorts each set of siblings by `z_index` (low first = behind, same convention, tie-broken by document order) before recursing — [[src.ui.document.cpp]] `child_stacking_order()`, shared with hit-testing so paint order and click/hover order always agree. A non-identity `transform: rotate()`/`scale()` calls `IUiPainter::apply_transform(center, radians, scale)` once, right after `scissor()` and before painting the element's own visuals and children — `layout_rect` itself is never transformed (paint-time only, same pattern as `ScaleWithScreenSize`'s letterbox scale), and children inherit the transform for free through NanoVG's own transform stack since `restore()` (already bracketing the element) undoes it, needing no separate "un-apply" call.

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
