---
tags: [module]
---

# UI

XML + custom CSS + C++ MVVM, plus an imperative `ui::Node` builder into the same `Element` tree. UI is an ECS component (`UiCanvas` + `UiInstance`), not a C++ widget graph and not a `Layout`/`onClick` tree.

## Capabilities

- Two document frontends: XML (`parse_xml`) and [[include.engine.ui.builder.h]] (`ui::Node` / `make_document`). Same tags/bindings; style remains CSS (no inline `width()` on the builder).
- `std::optional<AssetId> document` on `UiCanvas`. If set, Bind clones from `AssetsDb` when the id changes. If unset, `UiInstance` is canonical (`ui::spawn_canvas` clears the id). Bind/layout/paint do not care how the tree was created.
- `{binding path}` on `text` / `content` / `command` / `paint` / `source` / `items_source` / `pan-x` / `pan-y` / `zoom`, interned to a `BindingId` ([[include.engine.ui.binding_id.h]]) at parse time — not a runtime string lookup.
- CSS: element / class / id / `E.c`, descendant `A B`, child `A > B` (no `+`/`~`, no `,` grouping); `:hover` `:pressed` `:disabled`; units px/`%`/`em` + `calc(+ - * /)`; `@media (min-width|min-height: N)`; `@keyframes` (opacity only, via `animation-name`/`animation-duration`).
- Applied style: color, background, background-image, opacity, visibility, width, height, min-width, min-height, gap, flex-direction, padding, margin, justify-content, align-items, text-align, border-*, font-size, font-family (AssetId hex or `default`), animation-name, animation-duration, z-index, position, top/right/bottom/left, transform.
- `z-index` (sibling-local stacking order), `position: relative|absolute` (against the nearest positioned ancestor or the canvas root), `transform: rotate() scale()` (paint-time only, about the element's own center) — see [[include.engine.ui.document.h]] `PositionMode` ([[features/UI Markup]]). Rotated hit-test uses an AABB, not an oriented rect.
- `ICommand` / `RelayCommand` — only UI → game path.
- `IPaint` / `RelayPaint` / `IDrawList` — named VM paint into a layout hole after CSS chrome; private [[src.ui.draw_list_adapter.h]] maps local content px onto `IUiPainter`. `IUiPainter` stays private.
- Hit-test canvases by `order` (front first); within a canvas, siblings resolve topmost-first by the same `z-index` stacking order paint uses ([[src.ui.document.cpp]] `hit_test`). A hit is a **Button**, or any element with a bound `command` or `drag`, or a `<Viewport>` with a pan/zoom binding. Label/Stack/Canvas/Image without those do not consume. `:hover`/`:pressed` follow that same single topmost hit. `FillWindow` copies window size into `rect`. `ScaleWithScreenSize` (Unity `PanelSettings`-style) keeps layout/paint/hit-test in fixed `reference_size` design units and lets the engine derive one uniform letterbox `scale` + `offset` from `rect` — pixel-perfect scaling for a fixed pixel-art canvas at any window size, see [[include.engine.ui.canvas.h]] `canvas_layout_space`.
- `<Viewport>` is a paint-time 2D camera (pan/zoom bindings, empty-background pan drag, wheel zoom-to-cursor). `layout_rect` of children does not move. Ancestor clip uses intersecting scissor (`nvgIntersectScissor`); hit-test inverts the same camera and clips to the Viewport's unpanned box. Tooltips that must escape the clip belong on another `UiCanvas`.

## How it is implemented

- [[src.ui.xml_parser.cpp]] — tinyxml2; interns `{binding}` paths to `BindingId` via [[src.ui.bind_scan.h]].
- [[src.ui.builder.cpp]] — `ui::Node` factories/`add`/`make_document`; root must be Canvas.
- [[src.ui.canvas.cpp]] — begin_frame, handle_pointer, handle_wheel, apply_canvas_fit, canvas_layout_space, `spawn_canvas`. Pointer hit-test relayouts through `layout_painter_for` ([[src.ui.painter.h]] `UiLayoutPainters`) so hug text uses the same measurer as `paint_document`. Viewport pan uses `UiActivePans` (pointer delta), not 1D `ActiveDrag`.
- [[src.ui.css_parser.cpp]] — combinators, units, `calc()`, `@media`, `@keyframes`; unknown property → warning string, not fatal.
- [[src.ui.document.cpp]] — real content-box layout (padding/margin/gap/width/height/min-* affect sizing; Stack packs by used size + `justify-content`/`align-items`), bindings by `BindingId`; `position: absolute` children are split out of the packing/cursor loop and resolved against a threaded `containing_block` instead; `child_stacking_order()` (z-index sort) and `hit_test()`/`hit_bounds()` (topmost-first, AABB for rotated/scaled elements) are exported here and shared by paint.cpp and canvas.cpp.
- `ItemsControl` (`bind_element`, [[src.ui.document.cpp]]): normally clones one Element per `items_source` item every frame, reconciled by `ViewModel*` identity so an unchanged item keeps its Element (`animation_elapsed`, style/text-measure caches) across frames instead of rebuilding from the static `ItemTemplate`. When it is eligible, it instead generates only the visible window of rows plus a small overscan, flanked by up to two invisible "spacer" Elements (plain `Canvas`, no class/id, height = skipped-row count × row stride) that stand in for the scrolled-past rows so the unmodified `layout_stack` packing places the real rows at the right Y and computes the same `max_scroll_y` a full generation would. Eligibility requires `direction: vertical`, exactly one root Element in `ItemTemplate` with a fixed px `height`, and fixed px/absent `gap` — plus a scrolled viewport to window against, resolved as `effective_context`: the `ItemsControl` itself if it scrolls vertically (`overflow-y: scroll|auto`), otherwise the nearest scrollable ancestor at any nesting depth, threaded down through `bind_element`'s `scroll_context` parameter (updated to the current element whenever it `is_scrollable_y`, so the innermost scrollable ancestor always wins over an outer one) — `effective_context` must itself pack vertically. When `effective_context` is an ancestor rather than the `ItemsControl` itself, its `scroll_y`/viewport height apply through an `offset` (the `ItemsControl`'s absolute `layout_rect.y` minus `effective_context`'s — `layout_rect` is always absolute/canvas-space regardless of nesting depth, and unaffected by scroll since scroll is a paint-time transform, so this one subtraction is exact with any number of intermediate wrapper Elements and needs no direct-parent requirement); for the self-scrolling case the offset is definitionally `0`. If the resulting window would be empty — the `ItemsControl` scrolled entirely outside `effective_context`'s viewport while other content in it stays visible, only reachable when `effective_context` is an ancestor — it generates zero real rows and a single spacer covering the whole list (`N × row_height + (N-1) × gap`), preserving the same total-used-height invariant so a sibling that follows the `ItemsControl` (e.g. a footer) still lands where full generation would have put it. Row height can only be sampled from an already-styled generated row from a *previous* frame (a raw `ItemTemplate` never receives a CSS-resolved `height` — `apply_layout_style` skips its subtree); to survive the entirely-empty-window case (where no generated row is left to sample) it is cached persistently on the `ItemsControl` Element (`Element::virtualization_row_height_cache`) rather than re-derived every frame, refreshed whenever a live sample is available. Virtualization only engages starting the second frame after eligibility holds (self-correcting, like `max_scroll_y`/`layout_rect.h` staleness elsewhere in bind order). Falls back to full generation (unchanged) for a horizontal list, a multi-root `ItemTemplate`, a row without a fixed px `height`, or an `ItemsControl` with no scrollable context at all (neither itself nor any ancestor).
- [[src.ui.paint.cpp]] — cascade (incl. `@media`), interaction flags (topmost-hit-only via `hit_test()`), text position from justify/align/text-align, opacity keyframe sampling, `z-index` sibling sort before recursing into children, `IUiPainter::apply_transform` for non-identity rotation/scale, `IUiPainter::apply_view` for Viewport camera, calls `IUiPainter`. After background/border, invokes `Element::paint` (`IPaint`) if bound.
- [[src.ui.view_model.cpp]] — `property_`/`command_`/`paints_` maps keyed by `BindingId`, not name.
- [[src.ui.painter.h]] — private painter interface plus `UiLayoutPainters` (WindowId → `IUiPainter*` for hit-test layout).
- [[src.ui.draw_list_adapter.h]] — private `IDrawList` over `IUiPainter`.
- [[src.ui.splash.h]] — `ui::show_splash(world, config, image_size, window)` is a plain function a
  game calls explicitly (not engine-auto-triggered); `build_splash_document(config, image_size)`
  turns `SplashScreen`'s config into **two** in-memory documents, not one, via the same
  `parse_xml`/`parse_css` every other document goes through (no hand-built `Element`/`Keyframes`
  structs): a backdrop (`background: #000000`, no `animation-name` — constant, not faded — plus no
  children) and an image (fixed `position:absolute; left/top:10%; width/height:80%` `Image`, not
  `100%` — stretching would ignore the image's aspect ratio — animated via a 4-stop `@keyframes`
  opacity, plus `reference_size = image_size / 0.8`). `show_splash` spawns them via
  `ui::spawn_canvas` (no catalog document id) as two entities —
  backdrop `UiCanvas{fit=FillWindow, order=1000, window}` + `UiInstance`, image
  `UiCanvas{fit=ScaleWithScreenSize, reference_size, order=1001, window}` + `UiInstance`, both
  above every other canvas — because a single `ScaleWithScreenSize` canvas would letterbox the
  backdrop along with the image, leaving the game's own UI visible in the gap whenever the
  window's aspect ratio doesn't match the image's. `ScaleWithScreenSize`'s existing
  contain-fit letterboxes the image canvas into the real window preserving the image's aspect
  ratio, so the fixed 80% image box lands with a minimum 10% margin on every edge; `FillWindow`
  always makes the backdrop canvas's `rect` the full real window, so its opaque background covers
  the letterbox gap the image canvas leaves. `image_size` comes from
  `AssetsDb::get<render::TextureDesc>(config.image)`, resolved by the caller. Both entities carry
  a `SplashTimer{elapsed, total_duration}`; the engine system `run_splash_timers`
  (`src/ecs/systems.cpp`, registered by `register_engine_systems`) ages every `SplashTimer` by
  `Time::delta_time` each frame and `world.destroy()`s its entity once `elapsed >=
  total_duration` — both entities start together with the same `total_duration`, so they always
  despawn on the same frame.
- [[src.resources.codegen.cpp]] — for `importer = "ui"` XML, emits a binder struct (e.g. `assets::ui::Hud::bind(vm)`) with one `constexpr BindingId` per `{binding}` path; fails the build on an intern collision. `ViewModel` subclasses and `Bindable<T>` members stay hand-written.

NanoVG implementation: [[src.render.opengl.nanovg_painter.cpp]] — `Image` and CSS `background-image` paint via `IUiPainter::image(AssetId, Rect)` or `IUiPainter::image_nine_slice(AssetId, Rect, BoxInsets)` when `background-slice` or XML `<Image slice="...">` is specified, backed by an `AssetId`-keyed NanoVG image map populated at Init from `ImporterKind::Texture`/`UiImage` catalog entries.

Font faces: builtin UI font plus every catalog `ImporterKind::Font` registered in Init ([[features/Init and Loop]]).

## Public headers

- [[include.engine.ui.document.h]]
- [[include.engine.ui.builder.h]]
- [[include.engine.ui.stylesheet.h]]
- [[include.engine.ui.canvas.h]]
- [[include.engine.ui.view_model.h]]
- [[include.engine.ui.bindable.h]]
- [[include.engine.ui.binding_id.h]]
- [[include.engine.ui.command.h]]
- [[include.engine.ui.paint.h]]
- [[include.engine.ui.draw_list.h]]

## Tests

[[tests.ui_xml_test.cpp]] · [[tests.ui_builder_test.cpp]] · [[tests.ui_paint_binding_test.cpp]] · [[tests.ui_css_test.cpp]] · [[tests.mvvm_test.cpp]] · [[tests.ui_painter_test.cpp]] · [[tests.assets_test.cpp]] (codegen) · [[tests.splash_test.cpp]] · tests/ui_items_control_virtualization_test.cpp (ItemsControl virtualization)

## See also

- [[features/UI Markup]]
- [[features/UI Input]]
- [[modules/ECS]]
- [[build/Asset Codegen]]
