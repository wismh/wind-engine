# UI markup

XML (`src/ui/xml_parser.cpp`, tinyxml2) and `ui::Node` (`src/ui/builder.cpp`) build one `Element` tree. The tag list, attributes, and CSS property list are on [UI](../modules/UI.md). This page is the order those pieces run.

## Parse

Unknown tags call `IFatalError` when one was passed and return `UiError::UnknownElement`.

| Attribute | Accepted forms |
| --- | --- |
| `text`, `content` | literal, `{binding path}`, `{tr key}`, `{tr key name={binding path}}` |
| `formula` | literal or `{binding}`. `{tr}` is rejected |
| `command`, `paint`, `drag`, `pan-x`, `pan-y`, `zoom`, `scroll-x`, `scroll-y` | `{binding}` only |
| `source` | 32 lowercase hex, or `{binding}`. Not a filename |
| `checked`, `open` on `Popup` | Trimmed `true` or `1` sets it. Any other non-binding literal, including `false` and `0`, sets it false and is not an error. Or `{binding}` |
| `placement` on `Popup` | `bottom-start`, `bottom-end`, `top-start`, `top-end`, `right-start`, `right-end`, `left-start`, `left-end`. Anything else is `UiError::InvalidMarkup` |
| `items_source` | `{binding}`, or a literal copied into `Element::text`. The binding stays unbound. Parsed after `text`, so the literal replaces it |
| `display` on `Math` | bool literal |
| `allow-copy`, `allow-paste` | bool literals. Default true. Not bindings |
| `stylesheet` | on the root element, a CSS asset id |
| `src` on `ItemTemplate` | another XML document. A cycle is `UiError::CyclicInclude` |
| `var-<name>` | `{binding}` only, interned to a `BindingId`. A literal is `UiError::ForbiddenContent` |
| `gap` | `strtof` of the attribute, stored as px. `Stack`, `ScrollView`, and `Popup` only |
| `overflow` | `visible`, `hidden`, `scroll`, `auto`. Sets both axes. Any other token is not applied |
| `overflow-x`, `overflow-y` | the same tokens for one axis, after `overflow`. Any other token does not change that axis |
| `drag-orientation` | `vertical`, otherwise horizontal |
| `slice` | 1 to 4 lengths (`px`, `%`, `em`, or `calc()`; a bare number is px). Anything else is `UiError::InvalidMarkup` |

One `slice` length applies to every side, two to block and inline, three to top / horizontal / bottom, four to top, right, bottom, left. On an `Image`, `element.slice` is the nine-slice. If it is unset, paint uses CSS `background-slice`. If that is unset too, the image is not sliced.

`Node::gap`, `Node::slice`, and `Node::drag_orientation` (`src/ui/builder.cpp`) are the builder equivalents. `Node::overflow`, `overflow_x`, and `overflow_y` set the overflow fields.

An attribute that is still unknown is ignored. `var-` with an empty name is ignored. `Node::var` (`src/ui/builder.cpp`) is the builder path: an empty name or an unbound id does nothing.

`{binding}` is interned to a `BindingId` at parse time. The element keeps only the id. `parse_xml` also fills `UiDocument::binding_paths` (id to path, item templates included) so a bind error can name the path.

`Label` and `Button` text may contain `\(...\)`. `\\(` and `\\)` are the literals `\(` and `\)`. An unclosed `\(` stays plain text. A display formula is `<Math display="true">`.

`ScrollView` starts as a vertical stack with `overflow-y: auto`. `Stack` and `Popup` direction default to vertical. `horizontal` and `row` are the horizontal tokens.

## Style sheet

Selectors: `E`, `.c`, `#id`, `E.c`, descendant `A B`, child `A > B`. `+`, `~`, and comma groups are unsupported and warn.

Pseudo-classes: `:hover`, `:pressed`, `:disabled`, `:focus`, `:checked`.

`@media (min-width: N)` and `(min-height: N)`, and `@keyframes`, are parsed. Other at-rules warn.

Unknown properties warn and stay on the rule. The parser keeps going.

Every `parse_css` warning starts with the source line, `line N: ` (counted from the start of the text passed in). When `AssetsDb` loads a `.css` asset it sends each warning to the engine log, so a game sees them in `game.log`: `stylesheet ui/hud.css: line 3: unknown CSS property: frobnicate`. The asset cache makes that once per load; `unload_catalog` or a reload logs them again.

`--name: value` is stored at cascade time (`compute_style_uncached`); `is_known_property` accepts any `--` name. `var(--name)` and `var(--name, fallback)` substitute then, wherever they sit in the value: inside `calc()`, as one inset of `padding`, inside a fallback. A substituted value is resolved again, eight levels at most. The element's bound value wins over the sheet. A reference with no match and no fallback makes the whole value an empty string. Any other unknown property does not change computed style.

Units are `px`, `%`, `em`, and `calc()` with `+ - * /`. A `font-size` of `em`, including `em` inside a `font-size` `calc()`, multiplies by 16 (`kDefaultFontSize`). A percent `font-size`, including inside that `calc()`, uses the parent content width. Other lengths use the resolved font size as the `em` basis.

Color, gradient, `background-repeat`, easing, and `transform` values are on [UI](../modules/UI.md).

- Styles do not inherit. `compute_style_uncached` starts from a fresh `ComputedStyle` and does not copy the parent. Color, `font-size`, and `font-family` do not inherit. Defaults are white, 16px, and `builtin::font_ui`.

`display: none` removes the subtree from layout, paint, and hit-testing. Any other token sets `display_none` false, so a winning `display: block` shows the element again. It does not select another layout mode. `visibility: hidden` keeps the box.

## Bind

`run_bind` (`src/ecs/systems.cpp`), Frame / Bind:

1. If `UiCanvas::document` is set and the id changed, replace `UiInstance` from `AssetsDb::get<UiDocument>`. `spawn_canvas` clears that id, so a builder tree is not replaced.
2. Merge stylesheet asset ids with `try_get<Stylesheet>`. An empty list does not wipe a sheet already on the instance.
3. `apply_bindings` writes properties, commands, and paint ids from the view-model, then `{tr}` from `ctx<loc::Catalog>()`.

A binding the view-model does not register is skipped; the rest of the document still binds. `apply_bindings` reports each failure to its `IFatalError` (`UI paint binding "world" is not registered on the data context`; a builder document has no paths and shows `#<id hex>`) and returns the first error. `run_bind` passes a reporter that logs each message once per canvas with `log::warn`, prefixed by the document id, and remembers it in `UiInstance::reported_bind_errors`, so a missing binding is one log line, not one per frame. A new document starts a fresh set.

A bound command sets `disabled` from `!can_execute()` on every bind, except `TextInput`. See [UI Input](UI%20Input.md).

`ItemsControl` clones `ItemTemplate` once per `BindableList` element. The row's `generated_owner` is that item view-model.

`asset_codegen` scans `importer = "ui"` XML (`src/ui/bind_scan.h`) and emits, in `asset_ids.h`:

- one `constexpr BindingId` per `{binding}` path
- a struct such as `assets::ui::Hud` with `template<typename T> static void bind(T& vm)` that calls `vm.property`, `vm.command`, or `vm.paint` using the C++ member name

The scan (`collect_bind_element` in `src/ui/xml_parser.cpp`) reads every bindable attribute `parse_element` reads: `command` is a command, `paint` is a paint, and `text`, `content` (and each `{binding}` argument of a `{tr}` there), `formula`, `drag`, `checked`, `open`, `pan-x`, `pan-y`, `zoom`, `scroll-x`, `scroll-y`, `source`, `items_source`, and `var-<name>` are properties. A new bindable attribute goes in both places, or generated `bind()` misses it.

Two paths that hash to the same `BindingId` fail the build. Codegen does not emit a `ViewModel` class or the `Bindable` fields. The game writes those and calls `Hud::bind(*this)`.

A builder document has no generated binder. The game calls `intern` itself.

## Layout

`layout_element` in `src/ui/document.cpp`.

Stack children are packed by used size, margin, and gap. Used size is the specified size or the hug size, passed through `clamp_axis`: the max cap is applied first, then min-size wins, then the result is at least 0. Percent on a layout length resolves against the parent content box. `em` on those lengths uses this element's resolved font size. A `font-size` of `em`, including inside `calc()`, is a multiple of 16. A percent `font-size` uses the parent content width.

`justify-content` is the main axis (`start`, `center`, `end`, `space-between`). `align-items` is the cross axis. `text-align` moves glyphs and does not change the stack.

`white-space: normal` wraps at the width the element may take. `\n` breaks a row. `nowrap` is one row. Height is the row count times the used line height. A `\(...\)` is one unbreakable box. A horizontal stack does not split its width across labels. Give a label in a row a `width` or `max-width` if it should wrap.

Non-stack children (Canvas, Button, Label) overlay the same content rect. Each child still has its own resolved box.

`position: absolute` is taken out of flow. The containing block is the nearest `relative` or `absolute` ancestor, otherwise the canvas. Both opposite insets and no explicit size stretch that axis. `position: relative` stays in flow and is then offset by `top`/`left` (or the negation of `bottom`/`right`). Siblings were packed against the pre-offset size.

`Popup` is out of flow too. It hugs its content and is laid out at its parent's top-left; where it is shown is worked out later. See [UI](../modules/UI.md#popup).

`z-index` and `transform` do not change layout.

`layout_state_changed` compares text, custom properties, and the generated-owner list. A clean compare can skip `layout()` after the first successful layout. The comparison copies are still written every call.

## Paint

`paint_document` (`src/ui/paint.cpp`):

- Specificity (`compound_specificity` in `src/ui/paint.cpp`) is the sum of every compound in the chain, including ancestors. Element 1, class 2, element-and-class 3, id 4, and +10 for a pseudo on any compound. Two class compounds score 4, the same as one id. One `:hover` beats an id. Equal scores keep the later rule.
- `@media` is tested against the design box (`reference_size`) when the canvas is `ScaleWithScreenSize` and both sides are positive. Otherwise it uses `window_size_for` (`Presentation.sizes`).
- `:hover` is one `hit_test` result, the topmost element, not every box that contains the point.
- Siblings are stable-sorted by `z-index` ascending (document order on ties) via `child_stacking_order`. Hit-testing walks that order in reverse.
- A non-identity `rotate` / `scale` is `apply_transform` around the element and its children. `layout_rect` is not rewritten.
- `IPaint` runs after the element's CSS chrome and before its children, through `IDrawList` in local content pixels.
- `transition` and `@keyframes` sample numbers, colors, resolved px, and rotate/scale. Keywords snap at eased progress 0.5. Keyframes win over a transition on the same property. The clock is `delta_time` once per `paint_document`. Hit-testing during the animation uses the previous frame's rects.

`background-image` and `Image` `source` both call `IUiPainter::image`. `Viewport` paints its chrome, then `apply_view` for children.

`run_ui_render` sorts canvases by `order` (low behind) and pushes `CmdDrawUI`. A canvas with an open `Popup` gets a second command with `popup_layer` after every canvas's first, which draws only its popups, clipped by the window.

## Builder

`ui::Node` factories match the XML tags (`stack()`, `label()`, `viewport()`, `popup()`, …). `add` moves a child. `make_document` requires a `Canvas` root. `spawn_canvas` puts the tree on a world and clears `UiCanvas::document`.

## Tests

`tests/ui_xml_test.cpp`, `tests/ui_builder_test.cpp`, `tests/ui_css_test.cpp`, `tests/ui_paint_binding_test.cpp`, `tests/ui_painter_test.cpp`, `tests/ui_layout_hit_test.cpp`, `tests/ui_layout_dirty_gate_test.cpp`, `tests/ui_display_none_test.cpp`, `tests/ui_popup_test.cpp`, `tests/ui_bind_error_test.cpp`, `tests/assets_test.cpp`.

## See also

- [UI](../modules/UI.md)
- [UI Input](UI%20Input.md)
- [Asset Codegen](../build/Asset%20Codegen.md)
- [Localization](../modules/Localization.md)
