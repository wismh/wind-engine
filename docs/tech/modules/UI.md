# UI

XML and the C++ `ui::Node` builder produce one `Element` tree. Style is a CSS subset. A `ViewModel` supplies properties, `ICommand`, and `IPaint`. There is no `onClick` and no second widget graph.

Walkthroughs: [UI Markup](../features/UI Markup.md), [UI Input](../features/UI Input.md), [UI Inspector](../features/UI Inspector.md), [UI Profiler](../features/UI Profiler.md).

## Canvas

A `UiCanvas` is a component on an entity (`include/engine/ui/canvas.h`).

| Field | Role |
| --- | --- |
| `document` | When set, Bind clones `UiInstance` from `AssetsDb` if this id changed |
| `stylesheet`, `extra_stylesheets` | Asset ids merged in Bind |
| `data_context` | `shared_ptr<ViewModel>` |
| `rect` | Window pixels after fit |
| `reference_size` | Design resolution. Defaults to `{0,0}`. `ScaleWithScreenSize` uses it only when both sides are positive |
| `fit` | `FillWindow` (default), `Fixed`, or `ScaleWithScreenSize` |
| `order` | Paint and hit-test order. Low is behind |
| `window` | Which `WindowId` sizes this canvas. Default `kPrimaryWindow` |

`spawn_canvas` copies an in-memory `UiDocument` onto `UiInstance` and clears `canvas.document`, so Bind will not replace that tree from the catalog.

`apply_canvas_fit` runs from `begin_frame` and after a resize.

| `UiFit` | Layout space |
| --- | --- |
| `FillWindow` | `rect` is the window. Layout is in those pixels |
| `Fixed` | `rect` is left as authored. Layout is in those pixels |
| `ScaleWithScreenSize` | Both sides of `reference_size` positive: layout is that design box, letterboxed into `rect` (`offset` is the `rect` origin, `scale` is `rect.w / reference_size.x`). Otherwise `rect` is the full window, and layout is that rect at offset `{0,0}` and scale `1` |

## Documents

`ElementKind` (`include/engine/ui/document.h`):

| Tag | Kind |
| --- | --- |
| `Canvas` | Root. `make_document` / XML parse require it |
| `Stack` | Packs children on `direction` (`vertical` default, `horizontal` / `row`) |
| `ScrollView` | Stack that defaults to vertical and `overflow-y: auto` |
| `Label` | Text |
| `Button` | Text plus `command` |
| `Image` | `source` asset |
| `Checkbox` | `checked` literal or binding |
| `TextInput` | Editable string |
| `ItemsControl` | Clones `ItemTemplate` from `items_source` |
| `ItemTemplate` | The row. `src` includes another XML document |
| `Line` | Segment `x1,y1`–`x2,y2` |
| `Viewport` | Nested clip and camera (`pan-x`, `pan-y`, `zoom`). Not a document root |
| `Component` | Empty layout hole |
| `Math` | TeX in `formula`. `display="true"` is display style |

Unknown tags are `UiError::UnknownElement` and fatal when an `IFatalError` is passed.

Common attributes: `id`, `class` (space-separated), `name`. A custom property is only `var-<name>="{binding}"` (`parse_custom_properties` in `src/ui/xml_parser.cpp`), interned to a `BindingId`. A literal on that attribute is `UiError::ForbiddenContent`. `var-` with an empty name is ignored.

`Node::var` (`src/ui/builder.cpp`) is the builder path. An empty name or an unbound id does nothing.

`parse_element` also reads:

- `gap`: `strtof` of the attribute, stored as px. `Stack` and `ScrollView` only. `Node::gap` is the builder.
- `overflow` sets both axes. `overflow-x` and `overflow-y` then set one axis. Tokens are `visible`, `hidden`, `scroll`, and `auto`. Any other token is not applied. `Node::overflow`, `overflow_x`, and `overflow_y` are the builder.
- `drag-orientation`: `vertical`, otherwise horizontal. `Node::drag_orientation` is the builder.
- `slice`: 1 to 4 lengths (`px`, `%`, `em`, or `calc()`; a bare number is px). One value is every side, two are block and inline, three are top / horizontal / bottom, four are top, right, bottom, left. Anything else is `UiError::InvalidMarkup`. On an `Image`, `element.slice` is the nine-slice. If it is unset, paint uses CSS `background-slice`. If that is unset too, the image is not sliced. `Node::slice` is the builder.

An attribute that is still unknown is ignored.

`text` and `content` accept a literal, `{binding path}`, `{tr key}`, or `{tr key name={binding path}}`. The same attribute is not both `{tr}` and `{binding}`. `formula` rejects `{tr}`.

`command`, `paint`, `drag`, `pan-x`, `pan-y`, `zoom`, `scroll-x`, and `scroll-y` must be `{binding}` when present. `source` is 32 hex or `{binding}`, never a filename. `checked` is a literal or `{binding}`. A trimmed `true` or `1` sets it. Any other non-binding literal, including `false` and `0`, sets it false and is not an error.

`stylesheet` on the root element is a CSS asset id. `ItemTemplate src` includes another document. A cycle is `UiError::CyclicInclude`.

`ui::Node` (`include/engine/ui/builder.h`) builds the same `Element`. Factories match the tags. Bindings are `intern` ids. There is no codegen for a builder tree. `spawn_canvas` is how it goes on a world.

Inline math: a `Label` or `Button` string may contain `\(...\)`. `\\(` and `\\)` are the two-character literals. An unclosed `\(` stays plain text. A display formula stays `<Math display="true">`.

## Layout

`src/ui/document.cpp` lays out a content box.

Lengths are `px`, `%` (of the parent content box), `em`, and `calc()` with `+ - * /`. A `font-size` of `em`, including `em` inside a `font-size` `calc()`, multiplies by 16 (`kDefaultFontSize` in `resolve_font_size`). A percent `font-size`, including inside that `calc()`, uses the parent content width. Other lengths use the resolved font size as the `em` basis.

Stack main axis is the child's used size (explicit size, otherwise hug), plus margin and gap. Hug is text metrics for Label and Button, a default image size for Image, and a default checkbox size for Checkbox, then raised by `min-width` / `min-height`. `max-width` caps used width. `min-width` still wins over `max-width`.

`justify-content` places children on the main axis (`start`, `center`, `end`, `space-between`). `align-items` is the cross axis. `text-align` moves glyphs inside the content box and does not change the stack.

`white-space: normal` (the default) wraps at the width the element may take. `\n` breaks a row. `nowrap` keeps one row. A wrapped label's height is the row count times `line-height` (or the font's own line height when `line-height` is `normal`). A `\(...\)` is one unbreakable box on the row.

`display: none` drops the element and its subtree from layout, paint, and hit-testing. `visibility: hidden` keeps the space.

`position`:

| Value | Layout |
| --- | --- |
| `static` | In flow |
| `relative` | In flow, then nudged by `top` / `left` (or the negation of `bottom` / `right`). Siblings stay packed against the pre-offset size |
| `absolute` | Out of flow. Containing block is the nearest `relative` or `absolute` ancestor, else the canvas. Both opposite insets and no explicit size stretch the box |

`z-index`, `transform`, and opacity do not change layout sizes.

A layout dirty gate (`layout_state_changed`) compares `text`, `custom_properties`, and the generated-item owner list with the previous values. On the prepare path, `canvas.cpp` skips `layout()` when that compare is clean, `layout_computed_once` is set, and the canvas layout rect, media width, media height, stylesheet pointer, stylesheet generation, layout painter, and math-font identity all match the last layout. The copies are still stored every call.

`ItemsControl` virtualization (vertical, one template root, a fixed pixel row height) builds a window of rows plus spacers. `suppress_item_virtualization` turns it off. Variable-height windows and tree collapse are not this path. See [UI Performance Plan](../architecture/UI Performance Plan.md).

## Style

`src/ui/css_parser.cpp` parses a subset. An unknown property warns and stays on the rule. Unknown at-rules warn.

Selectors: type (`Label`), class (`.c`), id (`#id`), type-and-class (`Label.c`), descendant (`A B`), child (`A > B`). `+`, `~`, and comma grouping are rejected. Pseudo-classes: `:hover`, `:pressed`, `:disabled`, `:focus`, `:checked`.

At-rules: `@media (min-width: N)` and `(min-height: N)`, `@keyframes`. `transition` is a property, not an at-rule.

Known properties:

- Box: `width`, `height`, `min-width`, `max-width`, `min-height`, `padding`, `margin`, `gap`, `display`, `visibility`, `position`, `top`, `right`, `bottom`, `left`, `z-index`
- Flex-like: `flex-direction`, `align-items`, `justify-content`
- Text: `color`, `text-align`, `white-space`, `user-select`, `font-size`, `line-height`, `font-family`, `selection-color`
- Paint: `background`, `background-image`, `background-slice`, `background-repeat`, `opacity`, `border-radius`, `border-width`, `border-color`, `transform`
- Motion: `animation`, `animation-name`, `animation-duration`, `animation-delay`, `animation-timing-function`, `animation-iteration-count`, `transition`, `transition-property`, `transition-duration`, `transition-delay`, `transition-timing-function`
- Line: `x1`, `y1`, `x2`, `y2`, `stroke`, `stroke-width`
- Scroll: `overflow`, `overflow-x`, `overflow-y`, `scrollbar-width`, `scrollbar-color`, `scrollbar-thumb-color`, `scrollbar-track-color`, `scrollbar-thumb-hover-color`, `scrollbar-border-radius`

Styles do not inherit:

- `compute_style_uncached` starts from a fresh `ComputedStyle` and does not copy the parent.
- Color, `font-size`, and `font-family` do not inherit. Defaults are white, 16px, and `builtin::font_ui`.

`user-select` is `none` (default), `text`, or `all`. Buttons ignore it. An unknown value stays `none`.

Cascade specificity (`compound_specificity` in `src/ui/paint.cpp`) is the sum of every compound in the chain, including ancestors. A compound scores element 1, class 2, element-and-class 3, or id 4. A pseudo-class on that compound adds 10. Two class compounds score 4, the same as one id. One `:hover` beats an id. Equal scores keep the later rule.

`@media` is re-checked against the size passed into layout and paint. A `ScaleWithScreenSize` canvas with both `reference_size` sides positive uses the design box (`reference_size`). Otherwise the width and height come from `window_size_for` (`Presentation.sizes`). `:hover` is the single topmost hit, not every rect that contains the pointer.

A stylesheet `--name: value` is stored in `compute_style_uncached` even though `is_known_property` warned that the name is unknown. `var(--name)` and `var(--name, fallback)` substitute at cascade time, and only when the whole value is that call. The element's bound value wins over the sheet. No match and no fallback becomes an empty string. Any other unknown property does not change computed style.

`transition` and `@keyframes` interpolate numbers, colors, resolved px lengths, and `rotate` / `scale`. Keywords snap at eased progress 0.5. `@keyframes` wins on a property that also has a `transition`. The clock advances once per `paint_document` from `delta_time`. A layout property re-packs the chain that moved. Hit geometry during that animation is the previous frame's rects (`src/ui/style_anim.cpp`).

### Value grammar

`parse_color` (`src/ui/paint.cpp`) accepts only `#rgb`, `#rrggbb`, and `#rrggbbaa`.

`background` is one of those colors, or a gradient. `background-image` is a gradient, `none`, or a 32-hex `AssetId`.

- `linear-gradient` takes an optional angle, then stops. The angle is `N` or `Ndeg`. An omitted angle is 180 degrees. `to <side>` is not accepted.
- `radial-gradient` and `conic-gradient` take stops only. No shape and no position.
- A gradient needs at least two hex stops. A stop may have one percent, or a second percent as a hard stop. A missing percent is spaced by index across 0–100.

The stylesheet parser still warns when `background-image` is not `none` or an `AssetId`, including a gradient. The declaration stays, and paint accepts the gradient.

`background-repeat` defaults to no-repeat. The only token that tiles is `repeat`.

`transition-timing-function` and `animation-timing-function` (`parse_easing` in `src/ui/style_anim.cpp`) are `linear`, `ease`, `ease-in`, `ease-out`, or `ease-in-out`. `cubic-bezier` is rejected.

`transform` is `rotate(N)` or `rotate(Ndeg)`, and `scale(N)`, about the element center. Both may appear in one value.

## Bindings

`BindingId` is an FNV-1a hash of the path (`include/engine/ui/binding_id.h`). Zero means unbound. Two different paths that hash equal fail codegen (`CodegenErrorKind::Collision`).

`Bindable<T>` stores a value. `set` does not publish a version. `BindableList::get()` returns a mutable `vector&`, so a list can change with no signal.

`ViewModel::property`, `command`, and `paint` register those objects by `BindingId`. `assign_property_string` writes only when the formatted text differs. `write_property_float` / `write_property_string` write back when the registered type matches. Arithmetic types format with `std::to_string`.

`apply_bindings` (`src/ui/document.cpp`) resolves properties, commands, and paint against the view-model, then `{tr}` against a `Catalog` pointer. A bound command sets `disabled` from `!can_execute()`, except on `TextInput`. `ItemsControl` clones its template per list item.

`run_bind` clones from `AssetsDb` only when `UiCanvas::document` is set and the id changed. Stylesheet asset ids merge via `try_get<Stylesheet>`. An empty id list does not wipe a sheet authored in memory.

Generated binders (`asset_codegen`) emit `static constexpr BindingId` members and `bind(vm)` that calls `vm.property` / `vm.command` using the member names. The game writes the `ViewModel` subclass. See [Asset Codegen](../build/Asset Codegen.md).

`ICommand` is `can_execute` and `execute`. `RelayCommand` stores `std::function`. `IPaint::paint` receives an `IDrawList` and the content rect, after CSS chrome and before children. `IDrawList` is local content pixels: line, fill and stroke rect, arc, font, text, image. Games do not call NanoVG.

## Input

Pointer, wheel, keys, text, and IME enter as ECS events from [Input Mapper](../features/Input Mapper.md). `run_input` drains `MouseEvent` for the frame. Direct calls (`handle_pointer`, `handle_key`, `handle_text_input`, …) exist on `canvas.h` for tests and for code outside that system.

`MouseConsumed::consumed_windows` is a set of `WindowId` on the process `Presentation`. `reset_pointer_frame` clears it at the start of `simulate_worlds`. `begin_frame` does not. A hit inserts the window into `presentation_of(world).mouse`. `sync_frame` reads that same object for click-through. `world.ctx<ui::MouseConsumed>()` does not see the hits. See [Windowing](../features/Windowing.md).

`UiInputBatchCache` (`src/ui/input_batch.h`) lives on `run_input`'s stack. The first touch of a canvas in that call binds and lays out. Later mouse events in the same call reuse it. The public `handle_*` functions do not use the cache.

Details: [UI Input](../features/UI Input.md).

## Paint

`paint_element` (`src/ui/paint.cpp`) stable-sorts siblings by `z-index` (low behind, document order on ties) through `child_stacking_order`. Hit-testing uses the same function.

Per-element scissor is `nvgIntersectScissor`. `overflow` and `Viewport` clip. A non-identity `transform` calls `apply_transform` around the element's own visuals and its children. `layout_rect` stays axis-aligned.

`Viewport` paints chrome unpanned, then `apply_view` so children see `displayed = origin + zoom * (layout - origin + pan)`. Pan and zoom are bound floats.

`background-image` and `Image` `source` both call `IUiPainter::image(AssetId, rect)`. `run_ui_render` collects referenced images and fonts (`src/ui/ui_refs.h`) and calls `ensure_ui_image` / `ensure_ui_font`. `builtin::font_ui` is ensured even when no element names it.

`CmdDrawUI` carries the document, stylesheet, pointer, `delta_time`, the width and height used for `@media`, letterbox offset and scale, and optional inspector paths. A `ScaleWithScreenSize` canvas with both `reference_size` sides positive supplies that width and height from the design box (`reference_size`). Otherwise those values come from `window_size_for` (`Presentation.sizes`).

NanoVG is `src/render/opengl/nanovg_painter.cpp`, only in a windowed build. Tests use a recording `IUiPainter`.

## Text

`src/ui/text_wrap.cpp` breaks rows. `src/ui/text_select.cpp` maps clicks to caret indexes using the boxes paint stored (`PaintedTextLine` in `include/engine/ui/text_line.h`). `TextInput` also has `allow-copy` and `allow-paste`. The clipboard is `ctx<UiClipboard>`, filled by the SDL presentation.

## Math

`src/ui/math/` parses a TeX subset into an AST (`parse_formula` in `src/ui/math/math_parser.h`), lays it out with the OpenType MATH table of `builtin::font_math`, stretches glyphs, and paints outlines. `src/ui/math/math_element.cpp` is the `Math` element. `src/ui/inline_math.cpp` splits `\(...\)` inside label text. The math font is not loaded in `Engine::init`. `run_ui_render` uploads it when a formula references it.

The subset is:

- Letters and digits. A Latin letter becomes math italic. `h` is U+210E. Digits stay upright.
- Operators, `{}` groups, `^`, `_`, and `'`.
- `\frac`, `\sqrt[n]{}`, and `\vec`. The accent table is only `\vec`.
- Large operators `\sum`, `\prod`, `\coprod`, `\int`, `\iint`, `\iiint`, `\oint`, `\bigcup`, `\bigcap`, `\bigvee`, `\bigwedge`, `\bigoplus`, `\bigotimes`, with `\limits` and `\nolimits`. Integrals do not take limits in display style; the others do, until `\limits` or `\nolimits` overrides that.
- `\left`…`\right`.
- Greek letters (lowercase italic, uppercase upright), and relation, binary, and arrow symbols.
- `\text{}`, `\mathrm{}`, and `\operatorname{}`.
- Upright names from `kFunctionNames`: `sin`, `cos`, `tan`, `cot`, `sec`, `csc`, `arcsin`, `arccos`, `arctan`, `sinh`, `cosh`, `tanh`, `coth`, `log`, `ln`, `lg`, `exp`, `arg`, `deg`, `dim`, `ker`, `hom`, `lim`, `limsup`, `liminf`, `max`, `min`, `sup`, `inf`, `det`, `gcd`, `Pr`. From `lim` onward, those names take limits in display style.
- Spacing: `\,`, `\:`, `\;`, `\!`, `\quad`, `\qquad`, and a backslash followed by a space.

Parsing does not fail the document. `ParseResult::root` is always laid out. A rejected command is drawn as its source text. `errors` lists `ParseErrorKind`.

## Splash

`ui::show_splash` (`include/engine/ui/splash.h`) spawns two canvases on the given window: an opaque `FillWindow` backdrop and a `ScaleWithScreenSize` image. Both carry `SplashTimer`. `run_splash_timers` ages them with `Time::delta_time`, including while paused, and destroys them when `elapsed` passes `fade_in + hold + fade_out`. `nullopt` when `enabled` is false or the document cannot be built. The engine does not call `show_splash` itself. `image_size` is the decoded pixel size of `config.image`.

## Inspector and profiler

Public toggles are `set_inspector_enabled` and `set_ui_profiler_enabled`. Neither binds a key. The profiler functions compile to no-ops without `ENGINE_UI_PROFILER`. See the feature pages.

## Public headers

- `include/engine/ui/document.h`
- `include/engine/ui/builder.h`
- `include/engine/ui/canvas.h`
- `include/engine/ui/stylesheet.h`
- `include/engine/ui/view_model.h`
- `include/engine/ui/bindable.h`
- `include/engine/ui/binding_id.h`
- `include/engine/ui/command.h`
- `include/engine/ui/paint.h`
- `include/engine/ui/draw_list.h`
- `include/engine/ui/text_line.h`
- `include/engine/ui/splash.h`
- `include/engine/ui/inspector.h`
- `include/engine/ui/profiler.h`

## Tests

`tests/ui_xml_test.cpp`, `tests/ui_builder_test.cpp`, `tests/ui_css_test.cpp`, `tests/ui_layout_hit_test.cpp`, `tests/ui_layout_dirty_gate_test.cpp`, `tests/ui_display_none_test.cpp`, `tests/ui_scroll_test.cpp`, `tests/ui_items_control_virtualization_test.cpp`, `tests/ui_text_wrap_test.cpp`, `tests/ui_text_input_test.cpp`, `tests/ui_label_select_test.cpp`, `tests/ui_input_batch_test.cpp`, `tests/ui_paint_binding_test.cpp`, `tests/ui_painter_test.cpp`, `tests/ui_refs_test.cpp`, `tests/ui_loc_test.cpp`, `tests/mvvm_test.cpp`, `tests/ui_inline_math_test.cpp`, `tests/ui_math_parser_test.cpp`, `tests/ui_math_layout_test.cpp`, `tests/ui_math_paint_test.cpp`, `tests/ui_math_font_test.cpp`, `tests/ui_math_stretch_test.cpp`, `tests/ui_math_element_test.cpp`, `tests/splash_test.cpp`, `tests/ui_inspector_test.cpp`, `tests/ui_profiler_test.cpp`.

## See also

- [Localization](Localization.md)
- [Principles](../architecture/Principles.md)
