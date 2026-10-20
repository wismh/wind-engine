# UI

XML and the C++ `ui::Node` builder produce one `Element` tree. Style is a CSS subset. A `ViewModel` supplies properties, `ICommand`, and `IPaint`. There is no `onClick` and no second widget graph.

Walkthroughs: [UI Markup](../features/UI%20Markup.md), [UI Input](../features/UI%20Input.md), [UI Inspector](../features/UI%20Inspector.md), [UI Profiler](../features/UI%20Profiler.md).

## Canvas

A `UiCanvas` is a component on an entity (`include/engine/ui/canvas.h`).

| Field | Role |
| --- | --- |
| `document` | When set, Bind clones `UiInstance` from `AssetsDb` if this id changed |
| `stylesheet`, `extra_stylesheets` | Asset ids merged in Bind: the XML `<Canvas stylesheet>`, then `stylesheet`, then the extras. Rules and `@keyframes` of every sheet; a later `@keyframes` of the same name replaces the earlier one |
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
| `Slider` | `<Slider value="{binding v}" min max step/>`. See [Slider](#slider) |
| `TextInput` | Editable string |
| `ItemsControl` | Clones `ItemTemplate` from `items_source` |
| `ItemTemplate` | The row. `src` includes another XML document |
| `Line` | Segment `x1,y1`–`x2,y2` |
| `Viewport` | Nested clip and camera (`pan-x`, `pan-y`, `zoom`). Not a document root |
| `Component` | Empty layout hole |
| `Math` | TeX in `formula`. `display="true"` is display style |
| `Popup` | Vertical stack shown beside its parent, above every canvas of the window. See [Popup](#popup) |

Unknown tags are `UiError::UnknownElement` and fatal when an `IFatalError` is passed (a null one returns the error only).

Common attributes: `id`, `class` (space-separated), `name`. A custom property is only `var-<name>="{binding}"` (`parse_custom_properties` in `src/ui/xml_parser.cpp`), interned to a `BindingId`. A `Bindable<float>` or `Bindable<int>` substitutes its number as text, and a number with no unit is pixels in a length (`left: var(--x)`, `width: var(--w)`, inside `calc()`), so a position needs no string formatting; an `em` or `%` length still needs a `Bindable<std::string>`. A literal on that attribute is `UiError::ForbiddenContent`. `var-` with an empty name is ignored.

`Node::var` (`src/ui/builder.cpp`) is the builder path. An empty name or an unbound id does nothing.

`parse_element` also reads:

- `gap`: `strtof` of the attribute, stored as px. `Stack`, `ScrollView`, and `Popup` only (`packs_children`). `Node::gap` is the builder.
- `open` and `placement`: `Popup` only. See [Popup](#popup).
- `overflow` sets both axes. `overflow-x` and `overflow-y` then set one axis. Tokens are `visible`, `hidden`, `scroll`, and `auto`. Any other token is not applied. `Node::overflow`, `overflow_x`, and `overflow_y` are the builder.
- `drag-orientation`: `vertical`, otherwise horizontal. `Node::drag_orientation` is the builder.
- `slice`: 1 to 4 lengths (`px`, `%`, `em`, or `calc()`; a bare number is px). One value is every side, two are block and inline, three are top / horizontal / bottom, four are top, right, bottom, left. Anything else is `UiError::InvalidMarkup`. On an `Image`, `element.slice` is the nine-slice. If it is unset, paint uses CSS `background-slice`. If that is unset too, the image is not sliced. `Node::slice` is the builder.

An attribute that is still unknown is ignored.

`text` and `content` accept a literal, `{binding path}`, `{tr key}`, or `{tr key name={binding path}}`. The same attribute is not both `{tr}` and `{binding}`. `formula` rejects `{tr}`.

`command`, `paint`, `drag`, `if`, `pan-x`, `pan-y`, `zoom`, `scroll-x`, and `scroll-y` must be `{binding}` when present. `if="{binding}"` (any element, `Element::if_binding`, builder `Node::if_bind`) reads a `Bindable<bool>` or any arithmetic one: zero hides the element like `display: none`, so it leaves layout, paint, and hit-testing, and no winning `display` rule shows it again (`Element::hidden_by_if`). A name the view model does not register, or a `Bindable` that is not a number, is reported to the `IBindErrorSink` with the name, and the element stays shown. `source` is 32 hex or `{binding}`, never a filename. `checked` is a literal or `{binding}`. A trimmed `true` or `1` sets it. Any other non-binding literal, including `false` and `0`, sets it false and is not an error.

`stylesheet` on the root element is a CSS asset id. `ItemTemplate src` includes another document. A cycle is `UiError::CyclicInclude`.

`ui::Node` (`include/engine/ui/builder.h`) builds the same `Element`. Factories match the tags. Bindings are `intern` ids. There is no codegen for a builder tree. `spawn_canvas` is how it goes on a world.

Inline math: a `Label` or `Button` string may contain `\(...\)`. `\\(` and `\\)` are the two-character literals. An unclosed `\(` stays plain text. A display formula stays `<Math display="true">`.

## Element identity

Every `Element` of a `UiDocument` has an `ElementId` (`Element::handle`, `include/engine/ui/element_id.h`) and stores its parent's (`Element::parent`). An id is a slot of the document's `ElementArena` (`UiDocument::arena`, `include/engine/ui/element_arena.h`) and the generation that slot is on. `UiDocument::find(id)` returns the element or an `ElementLookupError`: `Null` (no id), `Stale` (the element is gone: its row left the window, the slot was freed or reused), `Misplaced` (the tree was edited by hand and not re-indexed). `element_lookup_error_name` names each. A lookup never returns a pointer to another element or to freed memory.

The tree stays `std::vector<Element>` by value: `children` and `generated_items` hold the elements, so an element may move or relocate (a vector grows, a document is copied or moved) and its id stays valid. An arena slot holds no `Element` address. It holds the parent's id, the position in the parent's `children` or `generated_items`, and which of the two. `find` climbs the parent slots to the root, walks the positions back down with bounds checks, and requires that the element it reaches carries the id. A pointer or an index into a `children` vector is not an id: it is invalid across any structural edit.

Who writes ids:

- `parse_xml` and `make_document` call `UiDocument::reindex()`. It gives an id to every element without one (an `ItemTemplate`'s subtree too), sets every `parent`, rewrites every slot's place, gives a fresh id to an element that copies another's, and frees the slots whose element is gone. An element that already has an id keeps it.
- The ItemsControl reconcile (`bind_element`, `src/ui/document.cpp`). A row the window keeps is moved into the new `generated_items` and keeps its id (and its motion clocks and caches, as before). A row cloned from the template, and every element under it, gets new ids: the clone's copied ids belong to the template. A spacer gets a new id each time. A row that leaves the window has its whole subtree's ids freed, and a freed slot comes back only on a new generation, so a dead id never names a later row. After the reconcile every row's `parent` and slot position are rewritten.
- `apply_bindings` calls `reindex()` for a tree built by hand and never indexed (a test that fills `children` itself).

`generated_owner` is unchanged. It is the identity of the data a row shows (the item view-model), compared and never dereferenced. The element id is the identity of the element. A reused row has the same owner and the same id. A row rebuilt for the same owner has the same owner and a new id.

Code that edits `children` or `generated_items` of a live document calls `reindex()` afterwards. Code that holds an element across frames holds its `ElementId`: the focus (`UiFocus::element`), a scrollbar drag (`ActiveScrollbarDrag::element`), and the ancestor chain of the style cache (`StyleCacheEntry::ancestors`, which never matches an ancestor without an id). A focus whose element is gone resolves to nothing and is dropped. The inspector's selection and a text-selection gesture keep a child-index path and a `generated_owner`, and resolve them again on every use (`src/ui/element_path.h`).

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
| `absolute` | Out of flow: adds nothing to the size its parent hugs, and is drawn and takes the pointer outside the parent's box unless the parent clips (`clips_children`: `overflow` other than `visible`, or a Viewport). In-flow children are still clipped to the parent's box ([UI Input](../features/UI%20Input.md#what-a-hit-is)). Containing block is the nearest `relative` or `absolute` ancestor, else the canvas. Both opposite insets and no explicit size stretch the box |

`z-index`, `transform`, and opacity do not change layout sizes.

What lays out again is decided by marks, not by a walk that compares the tree ([Dirty flags](#dirty-flags)). `sync_layout` (`src/ui/paint.cpp`) runs on both paths, `prepare_top_canvas` (`canvas.cpp`) and `paint_document`. It lays everything out when the document was never laid out or the canvas layout rect, the media width or height, the stylesheet pointer or generation, the layout painter, or the math-font identity differ from the last layout. Otherwise it restyles the elements marked `kDirtyStyle` and lays out the chain marked `kDirtyLayout` (plus the relayout boundaries), and a pass that finds nothing marked does nothing.

A `Popup` is out of flow and takes no space in its parent. See [Popup](#popup).

`ItemsControl` virtualization (vertical, one template root, a pixel `gap`, a vertical scroll context) builds a window of rows plus spacers. `suppress_item_virtualization` turns it off. Two shapes of row: a root with a fixed pixel `height` (the whole list is `N * height` plus gaps, as before), and a root of no fixed height (a label that wraps, a row of varying content), which is windowed over heights measured so far ([Rows of no fixed height](#rows-of-no-fixed-height)).

Eligibility is `bind_element` in `src/ui/bind.cpp`, and all of it must hold: `suppress_item_virtualization` is false, the template has one root, the `ItemsControl` is `Vertical` with a `px` gap, a vertical scroll context exists (the `ItemsControl` itself with `overflow-y` `scroll` or `auto`, or the nearest scrollable ancestor, which must be vertical and have a layout height), and either the first generated row has a `px` `height` (no `calc()`) or the rows have none. In the first case that `height` is the row height of the whole list, read from the generated row root; in the second the list is windowed over measured heights ([Rows of no fixed height](#rows-of-no-fixed-height)). A row with `position: absolute` is the exception: it adds no height to a stack, so a window computed from its height and the scroll offset would not match where the rows are drawn. `bind_element` therefore never virtualizes a list whose generated rows are absolute (it also forgets a row height cached before), and a list of 24 absolute rows builds 24 rows. The first frame builds every row either way, because the row height is sampled from the previous frame. A list laid out by `var-left` / `var-top` needs nothing else; a row that is in flow but must not virtualize still needs `flex-direction: row` on the list or no vertical scroll context. `suppress_item_virtualization` is not reachable from XML, CSS, or `ui::Node`: `advance_tree` (`src/ui/paint.cpp`) sets it for one bind while a row's `height`, `padding`, `gap`, or `font-size` is animating (`height_motion_active`). A tree is (see [Trees](#trees)). See [UI Performance Plan](../architecture/UI%20Performance%20Plan.md).

`scroll_item_into_view(document, scroller, items_control, index)` scrolls a list so one item lies inside the scroller's box. It reads the rows of the last layout and assumes one row height (or, for rows of no fixed height, the tops measured so far, estimates included), so it reaches an item the virtualized window has not built yet. It sets `Element::scroll_y` only and marks the scroller for paint (its frame). `find_by_id` finds an element by its `id` attribute, outside templates.

## Dirty flags

Three bits, independent: `kDirtyStyle` (the layout style of the element is written again from the cascade), `kDirtyLayout` (the element is laid out again), `kDirtyPaint` (it is painted again). Each element has `dirty_self` (what it needs) and `dirty_below` (what some descendant needs). `mark_dirty` (`src/ui/dirty.h`) sets `dirty_self` and walks up by `Element::parent`, setting `dirty_below` on each ancestor, and stops at the first ancestor that already has the bit. That is how a pass reaches the dirty element from the root without visiting the rest of the tree, and how `UiWorkCounters` and the per-element counters (`style_rebuilds`, `layout_runs`) let a test say "only that chain". The pass that consumes a bit clears it (`clear_dirty`, which follows `dirty_below`). Layout is the only bit that stops early, at a relayout boundary.

A relayout boundary is an element with a fixed `px` width and height, not `position: relative`, that has been laid out once (`is_relayout_boundary`): nothing below it can change its size or position. A layout mark from below stops at it, and the boundary goes on `UiDocument::layout_roots`; `sync_layout` lays it out again with `layout_from` from the rect and the parent content size and containing block of its last layout (`Element::layout_parent_content`, `layout_containing_block`), without its parent. A mark that starts in the boundary's own style (its size or margin may have changed: `own_box`) passes through it.

What marks what:

| Source of change | Marks |
| --- | --- |
| `text` / `content` binding, a `{tr}` result, text typed into a `TextInput` | layout and paint on the element |
| `if` binding | style, layout, paint (`own_box`): the cascade folds `hidden_by_if` into `display` |
| `var-` custom property, slider `--fraction` | style and paint (`own_box`); the restyle's geometry diff decides layout |
| `src`, pan, zoom, scroll, popup `open` | paint |
| a pseudo-class of the element (`set_pseudo`) | paint; style too only when a rule that also lays out tests that bit on it ([Hover and layout](#hover-and-layout)) |
| a restyle that moved a layout field (width, height, min/max, margin, padding, gap, direction, justify, align-items, white-space, font, line height, position, insets, overflow, display, visibility) | layout and paint (`own_box`) |
| an ItemsControl reconcile | layout and paint on the control; a cloned row and its subtree, and a spacer, want all three |
| a spacer's height | style, layout, paint (`own_box`) |
| canvas rect, media size, stylesheet pointer or generation, painter, math font | everything: the next `sync_layout` restyles and lays out the whole tree |
| motion | an element whose sampled value moved a layout field (`Element::layout_inputs_changed`, set by `advance_motion`) is marked `kDirtyStyle`; the restyle writes the sample onto the fields and marks layout where a field moved ([Style](#style)) |

Paint walks the whole tree until a repaint boundary exists, so it only clears its bit.

## Hover and layout

A pseudo-class may change layout: `.x:hover { padding: 8px }` restyles the element in the layout cascade and lays out the chain above it, on enter and on leave. It costs nothing where it is not used. At stylesheet parse time each rule records `subject_pseudo` (the bits on the subject compound), `ancestor_pseudo` (the bits on an ancestor compound, `A:hover B`), and `dirties` (`kDirtyStyle`, `kDirtyLayout`, `kDirtyPaint`: custom properties and `var()` are all three, geometry and text-shaping properties lay out, everything else only paints), and the line of its selector (`CssRule`, `parse_css`, `analyze_rule`). `style_analysis(sheet)` derives the rules that have a pseudo-class, the ones among them that also lay out, and the bits any rule tests on an ancestor.

- The layout cascade (`apply_layout_style`) reads the real pseudo-state for a rule that has a pseudo-class and lays out; a rule that only paints is not part of it, so an element that only such a rule touches keeps the layout cache it always had.
- A cascade records which pseudo bits it read (`StyleCacheEntry::self_dep`, `ancestor_dep[i]`: a rule that could match the element, or an ancestor, ignoring pseudo-classes, depends on that bit). A cache hit compares the state only through those masks. Hovering a container restyles only the descendants a rule like `A:hover B` can match, and a sheet with no ancestor pseudo-class rules restyles none.
- A pseudo-class change goes through `set_pseudo`: the element is marked for paint, and for style (so the layout cascade is written again, and layout is marked if a layout field moved) only when a layout rule tests that bit on it; descendants a layout rule can match through it are marked the same way.
- The hover target is not searched every frame. `apply_interaction` searches again only when the pointer or its button moved, a layout pass ran, something was marked (`UiDocument::epoch`), or motion is running, and a change of target sets the state on the two elements concerned, not on the whole tree. A hover that changes what lays out is laid out in the frame it starts, before the motion pass samples it.
- A rule that changes layout under a pseudo-class in a tree of more than 500 elements is named once per document in the log in Debug and editor builds (`warn_layout_under_pseudo`): the document, the stylesheet line, and the properties.
- A `:hover` that removes the element from under the pointer (`display: none`) un-hovers it, as in a browser; a transition on the property keeps the shown value in layout until it snaps.

## Debug layout check

A build with `ENGINE_UI_LAYOUT_CHECK` (always in Debug, otherwise `-DENGINE_UI_LAYOUT_CHECK=ON`; never in the editor, a player, or Release) lays a copy of the document out from scratch after every `sync_layout` (`src/ui/layout_check.cpp`): all caches and marks dropped, a full restyle, the motion values committed, a full layout. Every element's rect must equal the live tree's. A difference means a change reached the tree without marking what it feeds, so layout skipped work it owed: the check prints the first one with the element's path, counts it (`UiWorkCounters::layout_check_failures`), and ends the process (`UiDocument::abort_on_layout_mismatch`) so a suite cannot pass over it. `UiDocument::verify_layout` switches it off for a test that edits elements on purpose. The whole UI test suite and the bench scenes run under it.

## Style

`src/ui/css_parser.cpp` parses a subset. An unknown property warns and stays on the rule. Unknown at-rules warn. Each warning starts with `line N: `; every view the parser passes around is a substr of the source, so the line is the newlines before the view. `AssetsDb` logs a `.css` asset's warnings with `log::warn` when it loads it.

Selectors: type (`Label`), class (`.c`), id (`#id`), type-and-class (`Label.c`), descendant (`A B`), child (`A > B`). `+`, `~`, and comma grouping are rejected. Pseudo-classes: `:hover`, `:pressed`, `:disabled`, `:focus`, `:checked`.

At-rules: `@media (min-width: N)` and `(min-height: N)`, `@keyframes`. `transition` is a property, not an at-rule.

Known properties:

- Box: `width`, `height`, `min-width`, `max-width`, `min-height`, `padding`, `margin`, `gap`, `display`, `visibility`, `position`, `top`, `right`, `bottom`, `left`, `z-index`
- Flex-like: `flex-direction`, `align-items`, `justify-content`
- Text: `color`, `text-align`, `white-space`, `user-select`, `font-size`, `line-height`, `font-family`, `selection-color`
- Paint: `background`, `background-image`, `background-slice`, `background-repeat`, `opacity`, `border-radius`, `border-width`, `border-color`, `transform`
- Motion: `animation`, `animation-name`, `animation-duration`, `animation-delay`, `animation-timing-function`, `animation-iteration-count`, `transition`, `transition-property`, `transition-duration`, `transition-delay`, `transition-timing-function`
- Line: `x1`, `y1`, `x2`, `y2`, `stroke`, `stroke-width`
- Pointer: `cursor`
- Picture: `repaint-boundary` (`yes`, `true`, or `on` makes the element keep a picture of its own, [Pictures](#pictures); anything else does not; not pseudo-reactive)
- Scroll: `overflow`, `overflow-x`, `overflow-y`, `scrollbar-width`, `scrollbar-color`, `scrollbar-thumb-color`, `scrollbar-track-color`, `scrollbar-thumb-hover-color`, `scrollbar-border-radius`

Styles do not inherit:

- `compute_style_uncached` starts from a fresh `ComputedStyle` and does not copy the parent.
- Color, `font-size`, and `font-family` do not inherit. Defaults are white, 16px, and `builtin::font_ui`.

`user-select` is `none` (default), `text`, or `all`. Buttons ignore it. An unknown value stays `none`.

`cursor` is `auto` (default), `default`, `pointer`, `text`, `crosshair`, `wait`, `progress`, `move`, `not-allowed`, `ew-resize`, `ns-resize`, `nwse-resize`, or `nesw-resize` (`Cursor`, `<engine/ui/cursor.h>`). An unknown value stays `auto`. Like the layout fields it is read without pseudo-classes: a `:hover` or `:disabled` rule does not change it. It does not inherit in the cascade either, but `auto` takes the nearest ancestor's cursor when the window resolves it ([UI Input](../features/UI%20Input.md#cursor)).

Cascade specificity (`compound_specificity` in `src/ui/paint.cpp`) is the sum of every compound in the chain, including ancestors. A compound scores element 1, class 2, element-and-class 3, or id 4. A pseudo-class on that compound adds 10. Two class compounds score 4, the same as one id. One `:hover` beats an id. Equal scores keep the later rule.

### Safe area

`WindowSize::safe` (`SafeInsets`: `left`, `top`, `right`, `bottom`, window pixels) is what the OS covers or rounds off at each edge of a window: a camera cutout, rounded corners, a gesture bar, a system bar. It is zero on desktop and Web. `SdlGlPresentation::publish_size` fills it from `SDL_GetWindowSafeArea` (`WindowSystem::safe_insets`, scaled from window coordinates to drawable pixels) with the size, on attach, on resize, and on `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` (rotation, a bar shown or hidden). When the insets changed, a `WindowSafeAreaEvent` goes to that window's world. `Host::write_window_size` (the headless host) keeps the entry's insets and writes only the size.

A stylesheet reads them as `var(--safe-top)`, `var(--safe-right)`, `var(--safe-bottom)`, `var(--safe-left)` (`12px`), on any element, because they are not custom properties of the canvas root: Wind custom properties do not inherit. They live in `Stylesheet::environment`, a map the host writes with `set_environment` (a changed value takes a new `generation`, so style and layout caches rebuild; an equal one changes nothing). `apply_canvas_fit` writes the four for every canvas whose `UiInstance` has a stylesheet, and the stylesheet merge writes them into a sheet it has just built, so the first frame already has them. `substitute_vars` reads the environment after the element's own and the cascaded custom properties and before the fallback, so a rule that declares `--safe-top` wins, and a sheet nobody wrote the environment into (a test calling `apply_layout_style` directly) takes the fallback.

`canvas_safe_insets` turns the window insets into the canvas's: each edge of `rect` that lies inside an inset keeps the part of the inset beyond the gap to the window edge, divided by the layout scale (design units on a `ScaleWithScreenSize` canvas with a reference size). A canvas that stops short of an unsafe edge, such as the letterbox bars of a scaled canvas, gets `0` there.

```css
.hud    { padding: var(--safe-top) var(--safe-right) var(--safe-bottom) var(--safe-left); }
.floor  { height: calc(48px + var(--safe-bottom)); }
```

SDL reports non-zero insets on Android only while the activity draws edge-to-edge; otherwise the system already insets the window. Backgrounds stay full-window; put the inset on the element that holds the content. The editor's device presets (emulating a notch in the Game view) are not part of this.

`@media` is re-checked against the size passed into layout and paint. A `ScaleWithScreenSize` canvas with both `reference_size` sides positive uses the design box (`reference_size`). Otherwise the width and height come from `window_size_for` (`Presentation.sizes`). `:hover` is the single topmost hit, not every rect that contains the pointer.

A stylesheet `--name: value` is stored in `compute_style_uncached`; `is_known_property` accepts any `--` name. `var(--name)` and `var(--name, fallback)` substitute at cascade time wherever they sit in the value: inside `calc()`, as one inset of `padding`, inside a fallback. A substituted value is resolved again, eight levels at most. The element's bound value wins over the sheet, and the sheet over the host's environment ([Safe area](#safe-area)). A reference with no match and no fallback makes the whole value an empty string. Any other unknown property does not change computed style.

`transition` and `@keyframes` interpolate numbers, colors, resolved px lengths, and `rotate` / `scale`. Keywords snap at eased progress 0.5. `@keyframes` wins on a property that also has a `transition`. The clock advances once per `paint_document` from `delta_time`. `@keyframes` are compiled once into numeric tracks (`CompiledKeyframes`, one per animated property with its stops sorted, built on the first sample and kept on `Keyframes::compiled`; rebuilt when the stops were edited), so a sample parses no declaration string: it reads the two neighbouring stops and interpolates. `opacity`, `transform`, scroll, and a `Viewport` camera are paint only and mark no layout. A layout property (`width`, `height`, `min-*`, `max-*`, `font-size`, `padding`, `margin`, `gap`, ...) marks the animated element for layout on the frames its value moved, and only that chain lays out again: the element's own box can change, so the mark passes through a relayout boundary, the siblings and the rest of the tree are untouched. A finished `@keyframes` or transition keeps its end value in `motion_shown`, costs no cascade on later frames, and no longer keeps the hover target search running. Hit geometry during that animation is the previous frame's rects (`src/ui/style_anim.cpp`).

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

`Bindable<T>` stores a value and a version. `set` compares with `==` (a type without `==` always counts as changed) and, on a change, raises `version()` by one and calls the one handler registered with `on_changed(std::function<void(const T&)>)`, synchronously, once. A write from C++ and a write from the document (`ViewModel::write_property_float` / `write_property_string`: drag, `TextInput`, `Checkbox`, popup close) take the same path; an unchanged value is silent and does not move the version. The handler runs inside input handling for a document write, so it must not destroy the view model or its canvas. `BindableList` has a version too and no mutable `get()`: it changes through `set(vector)` and `edit(fn)`, and each raises the version once, only when the items differ afterwards (`edit` compares against a copy taken before). Nothing subscribes: the bind pass polls the versions. `Catalog::version()` moves when a table, the active or fallback locale, or the pseudo mode changes.

`ViewModel::property`, `command`, and `paint` register those objects by `BindingId`. `assign_property_string` writes only when the formatted text differs. `write_property_float` / `write_property_string` write back when the registered type matches. Arithmetic types format with `std::to_string`.

`apply_bindings` (`src/ui/bind.cpp`) resolves properties, commands, and paint against the view-model, then `{tr}` against a `Catalog` pointer. A bound command sets `disabled` from `!can_execute()`, except on `TextInput`. `ItemsControl` clones its template per list item.

The first call for a document, and any call after the data context, the catalog pointer or its version, or the tree's structure (an ItemsControl reconcile that dropped rows) changed, walks the tree, reads every binding, and records a flat table of slots in `UiDocument::bind_table`: the element id, the field (`BindField`: text, content, source, `if`, slider value, pan, zoom, scroll, checked, open, `var-` custom property, `{tr}` argument, `items_source`, command, paint), the `BindingId`, the view-model the slot reads (the canvas data context, or a row's item inside an ItemsControl), and the property version it last saw. Every other call is one integer compare per slot (`UiWorkCounters::slots_checked`); a slot whose version moved reads its one property again (`bound_reads`) and marks what that field feeds dirty only if the value differs. A bound command has no version: `can_execute()` is polled each call and a change sets `disabled`. A slot whose element a reconcile dropped is dropped with it, so no slot outlives the view-model of a row that left the list. A scroll position and an ItemsControl window are read again after a layout pass (`UiDocument::windows_dirty`), because they depend on the layout's extents and not only on the property. The failures of the build (a binding the view-model lacks) are kept and reported again by every call, like the old walk did. Something that edits a field a binding fills behind the bind pass keeps its edit until the property changes.

Without `EngineSystemDeps::assets` the clone and stylesheet systems are not registered. `run_bind` itself only applies bindings. With assets, it clones from `AssetsDb` only when `UiCanvas::document` is set and the id changed. Stylesheet asset ids merge via `try_get<Stylesheet>`; one missing from the catalog logs a warning once per world and is retried each frame. An empty id list does not wipe a sheet authored in memory.

Generated binders (`asset_codegen`) emit `static constexpr BindingId` members and `bind(vm)` that calls `vm.property` / `vm.command` using the member names. The game writes the `ViewModel` subclass. See [Asset Codegen](../build/Asset%20Codegen.md).

`ICommand` is `can_execute` and `execute`. `RelayCommand` stores `std::function` and fits tests and quick scripts. `MethodCommand` is the command of a button on an owner: an owner pointer and a function pointer (no `std::function`, no allocation), bound after construction with `bind_to<Owner, &Owner::method[, &Owner::can_method]>(owner)`; unbound it cannot execute, and binding again replaces the owner. `IPaint::paint` receives an `IDrawList` and the content rect, after CSS chrome and before children. `IDrawList` is local content pixels: line, fill and stroke rect, arc, font, text, image. Games do not call the painter or GL.

## Input

Pointer, wheel, keys, text, and IME enter as ECS events from [Input Mapper](../features/Input%20Mapper.md). `run_input` drains `MouseEvent` for the frame. Direct calls (`handle_pointer`, `handle_key`, `handle_text_input`, …) exist on `canvas.h` for tests and for code outside that system.

`MouseConsumed::consumed_windows` is a set of `WindowId` on the process `Presentation`. `reset_pointer_frame` clears it at the start of `simulate_worlds`. `begin_frame` does not. A hit inserts the window into `presentation_of(world).mouse`. `sync_frame` reads that same object for click-through. `world.ctx<ui::MouseConsumed>()` does not see the hits. See [Windowing](../features/Windowing.md).

`UiInputBatchCache` (`src/ui/input_batch.h`) lives on `run_input`'s stack. The first touch of a canvas in that call binds and lays out. Later mouse events in the same call reuse it. The public `handle_*` functions do not use the cache.

Details: [UI Input](../features/UI%20Input.md).

### Rows of no fixed height

A reconcile finds the row of an item by `ViewModel::serial()`, a number never reused, not by the item's address: a view-model destroyed and another created in its place share an address, and the new item must not get the old item's row. `ViewModel::registration_version()` goes up when `property`, `command` or `paint` registers something; the bind table is built again when it moved for the data context or for the view-model of any slot, so a binding registered late or a paint replaced is found.

A row whose root has no pixel `height` is virtualized too, once the list has been laid out. The first bind cannot know any height (no layout has run, no scroller has a size), so it builds every row; the next pass reads each row's `layout_rect.h` into `Element::row_heights` (`measured`, keyed by `generated_owner`, the item's view-model) and from then on builds a window. The tops of all rows (`tops`, a prefix sum of the measured heights plus the gap) are rebuilt when a measure changed, a row was added, or the item list differs; a row never measured counts as the mean of the measured ones until it has been laid out once. The first and last row of the window come from a binary search over `tops` (plus two rows of overscan), and the two spacers are the heights of the rows before and after it. A row that leaves the window keeps its measured height, so scrolling back builds the same spacers; entries of items that left the list are dropped once the map is twice the list. Unchanged heights do not rebuild anything: a quiet scroll costs the search and the rows it enters.

What it does not do: the scroll offset is not anchored. When a row above the window measures differently than its estimate, content moves under the pointer by the difference. Heights are not remeasured for rows outside the window when the width changes (wrapped text); they are when they come back. Rows that are `position: absolute` still never virtualize.

Text is measured once per distinct `(text, font, size)` and wrapped once per `(text, font, size, width)` for a whole document: `UiDocument::text_cache` (`src/ui/text_cache.h`) sits behind the per-element caches, so a row cloned from the template, a recycled row, and a row with the same text as a hundred others do not reach the painter again. It is dropped when the painter changes and bounded in size.

## Paint

`paint_element` (`src/ui/paint.cpp`) orders siblings by `z-index` (low behind, document order on ties) through `child_stacking_order`. Siblings that already ascend by `z-index`, every one equal included, are not sorted at all. Hit-testing uses the same function.

Pictures: a boundary replays what it recorded ([Pictures](#pictures)). Per-element scissor intersects with the clip above it (the outer clip's bounding box in the current transform): every element draws and passes its in-flow children under its own box. An element enters no painter state (`save`, `set_opacity`, `apply_transform`, `scissor`, `restore`) when it has nothing to set: its opacity is the one the painter already holds (`state_opacity`, the nearest entered ancestor's; a canvas root and a popup always set theirs), it has no transform, and its scissor would clip nothing. `paint_stays_in_box` decides the last: a text-only or box-only element with no border, whose measured text fits its content box and whose in-flow children lie inside its box and carry no transform. An absolute descendant of an in-flow child leaves that child's scissor and is still under this one, so it has to lie inside the box too, looked for through the elements that do not clip (at most 256 elements, then the scissor stays). A sheet with a pseudo-class rule that declares `transform` (or a `var()`, which could feed it) keeps the scissor of every element that has children, because a pointer can move a descendant past the box in a way the layout style does not show. A border (its stroke is centred on the edge), a `Line`, `Math`, `TextInput`, `Viewport` or `ScrollView`, a custom `IPaint`, `overflow` other than `visible`, and anything the measure caches cannot vouch for keep the scissor. A held motion sample is drawn from a style that carries only the fields `paint_element` reads (`copy_paint_fields`), not a copy of the whole cascade. `overflow` and `Viewport` clip. A non-identity `transform` calls `apply_transform` around the element's own visuals and its children. `layout_rect` stays axis-aligned.

`Viewport` paints chrome unpanned, then `apply_view` so children see `displayed = origin + zoom * (layout - origin + pan)`. Pan and zoom are bound floats. A camera paints what its box shows: a child with its subtree wholly outside the visible part of the world (`[origin - pan, origin - pan + size / zoom]` in layout space, with a 16 px margin) is not painted, at any depth until a scroll, a transform, or another camera changes the space (`Element::culled_children` counts them). A child is only culled when its reach is known: an element with a transform, an `IPaint`, a `Line`, `Math`, `TextInput`, `Popup`, a nested `Viewport`, text that was not measured, or a style never resolved counts as reaching the view, and one that clips its children reaches only its box. A world drawn by an `IPaint` culls itself. Hit-testing is not culled.

`background-image` and `Image` `source` both call `IUiPainter::image(AssetId, rect)`. `run_ui_render` collects referenced images and fonts (`src/ui/ui_refs.h`) and calls `ensure_ui_image` / `ensure_ui_font`. `builtin::font_ui` is ensured even when no element names it.

`CmdDrawUI` carries the document, stylesheet, pointer, `delta_time`, the width and height used for `@media`, letterbox offset and scale, optional inspector paths, `popup_bounds` (the window in layout units), and `popup_layer`. A `ScaleWithScreenSize` canvas with both `reference_size` sides positive supplies that width and height from the design box (`reference_size`). Otherwise those values come from `window_size_for` (`Presentation.sizes`).

The windowed painter is `ui::UiRenderer` (`src/ui/render/`) with an `OpenGLUiDevice` ([Render](Render.md#opengl-engine_with_window)); it is the only painter. `fill_path` rasterizes a filled contour into the mask atlas. `stroke_path` strokes the same segments (round cap, round join, closed only on `Close`) into that atlas, and the cache key includes the device-pixel width. Fonts, glyph, mask, ramp, and image atlases are shared by every window (`ui::PaintResources`). `reset_ui_painter` (editor Stop) keeps the fonts registered and drops the images; the next frame uploads the ones still referenced. Tests use a recording `IUiPainter`, and `tests/ui_renderer_test.cpp` drives `UiRenderer` against a fake `IUiDevice`.

### Pictures

A **repaint boundary** keeps what it paints as a `Picture` (`src/ui/picture.h`): the list of `IUiPainter` calls its subtree made, in order, with their arguments. It is not a GPU command stream and not a texture; any painter replays it, and `RecordingPainter` writes one without a GPU (measuring is answered by the painter it wraps), which is what `engine_tests` use to see what a document paints. The boundaries are the root, anything that scrolls (`overflow` `scroll` or `auto`, a `ScrollView`), a `Viewport`, a `Popup`, and an element the stylesheet marks with `repaint-boundary: yes` (`Element::repaint_boundary`, from the layout style, so not pseudo-reactive). Everything else is a plain element and is recorded into the picture of the nearest boundary above it.

A boundary owns three pictures in `Element::picture`:

| Part | Holds | Rewritten |
| --- | --- | --- |
| `chrome` | the element's own drawing: box, border, text, custom `IPaint` | when the element holds a paint mark, or the key moved |
| `content` | its children; a nested boundary appears as a `Call` of that boundary's frame | when a plain element in its region holds a mark, or the key moved |
| `frame` | the state the element sets itself (opacity, transform, scissor, scroll offset, camera), the calls of `chrome` and `content`, and the scrollbar | every time paint reaches the element: a few commands |

So opacity, transform, scroll and a `Viewport` camera are parameters of the frame: the picture below them is not rewritten. The key (`PictureKey`) is the element's rect, the canvas offset and scale, the parent content size, the window size, and the stylesheet and its generation. A boundary records its children with the opacity of its caller unknown, so what it holds does not depend on its parent's opacity.

Marks: `kDirtyPaint` rewrites the boundary that holds the element (the element itself when it is one); `kDirtyFrame` (scroll offset, camera, scrollbar hover and drag, an animated opacity or transform on a boundary) rewrites only its frame. A layout pass that moved or resized an element marks it for paint: `layout` and `layout_from` record the elements whose rect changed, the flow children they slid and the positioned ones they nudged included (`MovedElements`, `src/ui/painter.h`), so only those are marked, not the whole tree. The pass that draws follows `dirty_below` from the root: a boundary whose region holds no mark and whose key still matches is not visited, so a quiet frame replays the root's frame and paints no element (`UiWorkCounters::painted_elements` does not move, `pictures_replayed` goes up by one).

Pictures that cannot be replayed are *volatile* and recorded every frame, as paint was before: a focused `TextInput` (the caret blinks with the clock), an `IPaint` whose `version()` is `kVersionUnknown` (the default), and anything that measured a non-empty string as zero wide (a font that is not ready). An `IPaint` that returns a version, with the same meaning as `Bindable::version`, is recorded again only when it moves. A `Viewport` records its children for half a screen of world more than its box shows on every side and replays them while the camera looks at nothing outside that; children wholly outside the recorded world are culled ([Paint](#paint)).

A hidden element (`display: none`, `visibility`, a closed popup) holds no mark that any picture above cares about; showing it marks it again.

The debug check of [Debug layout check](#debug-layout-check) covers pictures: after each paint, every boundary records afresh and is compared with the picture it holds, and a difference means a change reached an element without marking it for paint (`UiWorkCounters::picture_check_failures`).

## Slider

`<Slider value="{binding level}" min="0" max="100" step="5"/>` is a value picker over the `drag="{binding}"` machinery ([UI Input](../features/UI%20Input.md#drag-and-scroll)). `value` is a `{binding}` to a `Bindable<float>` (or any number) and is required; `min` (default 0), `max` (default 1, must be above `min`), and `step` (default 0, continuous; not negative) are literals. `drag-orientation="vertical"` makes it vertical; a horizontal slider grows to the right, a vertical one **upward**. XML children are ignored: the parser (or `ui::slider()`) generates two plain Stacks, `children[0]` with class `slider-fill` and `children[1]` with class `slider-handle`. `slider_fraction` and `slider_snap` (`document.h`) are the arithmetic.

**Placement is the stylesheet's.** Every bind sets the custom property `--fraction` (0 to 1, from the value, clamped to the range) on the slider and on both parts, so CSS alone positions them, and the layout dirty gate relayouts when it changes. The slider is a containing block, so `100%` in the parts' insets is the track:

```css
.volume { width: 200; height: 20; background: #222; }
.slider-handle { --handle: 20px; position: absolute; top: 0; width: var(--handle); height: 20;
                 left: calc(var(--fraction) * (100% - var(--handle))); background: #ccc; }
.slider-fill { position: absolute; left: 0; top: 0; height: 20; width: calc(var(--fraction) * 100%); background: #58a; }
```

The handle width is written once, in CSS (`--handle`). The engine reads it back from the laid-out handle: a drag maps the pointer to the value as `(pointer - track start - handle / 2) / (track length - handle)`, clamped to 0..1 (a vertical slider turned over), so the handle's centre follows the pointer and the ends reach `min` and `max` exactly. The value is `min + t * (max - min)`, snapped to the nearest multiple of `step` from `min`, and written with `write_property_float` on the slider's owner (the row view-model inside an `ItemsControl`). A drag keeps writing while the pointer is outside the track, like any `drag`. A missing `value` name or a non-numeric `Bindable` is reported to the `IBindErrorSink` (`UI value binding "name" ...`). `Bindable::on_changed` fires once per snapped value, so a game reacts without polling. `translate()` is not part of the CSS `transform` subset: this placement needs no pixel constant in two places, so it was not added.

A missing name on any `drag` binding is now reported too (`UI drag binding "name" is not registered on the data context`); it used to be skipped silently.

## Popup

A menu, dropdown, or context menu that no ancestor clips and every canvas of the window is under. `src/ui/popup.h` is the private API.

```xml
<Button class="more" command="{binding toggleMenu}">
  <Popup open="{binding menuOpen}" placement="bottom-end">
    <Button content="Rename" command="{binding rename}"/>
    <Button content="Remove" command="{binding remove}"/>
  </Popup>
</Button>
```

- The anchor is the Popup's parent. Inside an `ItemTemplate` each row has its own popup and its own `open`.
- `open` is a literal (`true` / `1`) or a `{binding}` to an arithmetic `Bindable` (a `bool` is fine). The engine writes `0` through that binding when it closes the popup, on the row view-model (`generated_owner`) inside an `ItemsControl`. The view-model opens it, and closes it from a menu item's command.
- `placement` is `bottom-start` (default), `bottom-end`, `top-start`, `top-end`, `right-start`, `right-end`, `left-start`, or `left-end`: the side of the anchor, then which edge it lines up with. Any other token is `UiError::InvalidMarkup`. `Node::popup()`, `open`, `open_bind`, and `placement` are the builder.

Layout: a Popup packs its children like a vertical `Stack` (`direction`, `gap`, `flex-direction` apply). It is out of flow. It hugs its content with no width to wrap against, so `max-width` is what makes text wrap. Percentages resolve against the anchor's content box. Layout leaves it at the anchor's top-left, and it is the containing block of its absolute descendants. A closed popup is still laid out, so opening one does not need a relayout.

Placement: `place_popups` runs after every layout in `paint_document` and on every input event (`prepare_canvas`), because scroll and a Viewport camera move an anchor without a relayout. It maps the anchor's border box through its ancestors' scroll and camera, puts the popup on the `placement` side with the margin facing the anchor as the gap, and flips to the opposite side when the popup does not fit and that side has more room. Then it is pushed inside the window (`popup_bounds`: the window in the canvas's layout units, or the canvas rect while the window has no size). It stores `shown - layout` in `Element::popup_offset`. A popup is never scaled by an ancestor Viewport's zoom, and ignores ancestor `transform` and opacity. A nested popup is placed from where its parent popup is shown.

Every walk skips Popup children: paint, `hit_test`, `hit_test_visual`, `find_scrollable_at`, and `find_viewport_at` visit the open popups first (`open_popups`, the last drawn first) from their offset, then the tree. `layout_boxes` maps a popup's subtree through its offset.

Paint: the base pass does not draw popups. `run_ui_render` pushes a second `CmdDrawUI` with `popup_layer` for each canvas with an open popup, after every canvas's base command, so a popup is above canvases of higher `order` too. That pass scissors to the window, then draws each open popup through `apply_view` by its offset, in document order (siblings by z-index, a nested popup after its parent). With a popup open, the inspector boxes are drawn in that pass, above the popups.

Hover: while an open popup is under the pointer, every other canvas of that window gets a `CmdDrawUI` pointer far outside, so nothing under the popup shows `:hover`.

Input is on [UI Input](../features/UI%20Input.md#popups).

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

## Trees

A tree is a flat list of rows, not a nested control. `include/engine/ui/tree.h` turns a caller's tree into rows; the document is a virtualized `ItemsControl` with one row template. There is no `TreeView` tag and no recursive template, so a collapsed branch is never a row: its children are not bound or laid out.

| Name | Does |
| --- | --- |
| `TreeExpansion<Key, Hash>` | Which nodes are expanded, by a key that survives a rebuild. Expanded by default, or collapsed with `TreeExpansion(false)`. Stores only the keys that differ from the default. `retain` drops keys of gone nodes |
| `flatten_tree(roots, expansion, source, row)` | Depth first. Calls `row(node, info)` once per visible row and returns the `TreeRowInfo`s. `source` has `key`, `has_children`, and `for_each_child`; a collapsed node's children are never made |
| `TreeRowInfo` | `depth`, `has_children`, `expanded` (false on a leaf), `parent` row (`kNoTreeRow` on a root) |
| `tree_navigate(rows, current, nav)` | Up, Down, First, Last move. Left collapses an expanded row, otherwise goes to the parent. Right expands a collapsed row, otherwise goes to the first child. Returns the row to select and whether to toggle it |
| `tree_nav_for_key(key)` | Arrows, Home, and End. A tree reads `KeyEvent`, repeats included, like other UI, not an `ActionId` |

The row recipe (the editor's UI Tree and Project tabs use it):

```xml
<ScrollView id="tree" class="tree">
  <ItemsControl id="tree-rows" items_source="{binding rows}">
    <ItemTemplate>
      <Stack class="row" direction="horizontal" var-depth="{binding depth}">
        <Checkbox class="expander" checked="{binding expanded}" command="{binding toggle}"/>
        <Button content="{binding label}" command="{binding select}"/>
      </Stack>
    </ItemTemplate>
  </ItemsControl>
</ScrollView>
```

```css
.row { height: 22; padding: 0 0 0 calc(var(--depth, 0) * 14px); }
Checkbox.expander { width: 12; height: 12; background-image: a0e1b2c3d4f5678901234567890abc07; transition: transform 0.12s ease-out; }
Checkbox.expander:checked { transform: rotate(90deg); }
Checkbox.expander:disabled { background-image: none; }
```

The indent is padding on the row, so the chevron moves with the text. `builtin::tree_chevron` points right; `:checked` turns it down. A leaf's `toggle` cannot execute, so its expander is `:disabled` and draws nothing. Keep the row height fixed, or the list is not virtualized.

## Docking

`include/engine/ui/dock_layout.h` and `dock_geometry.h` are the dock model: tab stacks, splits, and floats keyed by panel name, their rects, chrome and drop hit tests, and TOML text. Plain data; no canvas, input, or font.

`include/engine/ui/dock_space.h` is the host: a `DockSpace` component (window, area, metrics, layout, panels with their `UiCanvas` entities, order base, chrome stylesheets, `revision`). `run_dock_input` (Input, after `run_input`) handles presses and drags on the chrome and consumes the mouse there; `run_dock_layout` (Bind, before `run_bind`) writes every panel canvas's `rect`, `order`, `window` (`Fixed`; a hidden tab is an empty rect) and keeps the engine's chrome canvases (`ui::Node` document, `builtin::dock_css`). Tabs are as wide as their titles in the theme's `.dock-tab` font and padding, measured with the window's painter. `float_mode` says where floats live: virtual windows clamped inside the area, or an OS window each (`IWindowControl` and `Worlds` from `EngineSystemDeps`), with tabs dragged between the space's windows. The editor's panels use it, with OS windows. See [Docking](../features/Docking.md#host).

## Splash

`ui::show_splash` (`include/engine/ui/splash.h`) spawns two canvases on the given window: an opaque `FillWindow` backdrop and a `ScaleWithScreenSize` image. Both carry `SplashTimer`. `run_splash_timers` ages them with `Time::delta_time`, including while paused, and destroys them when `elapsed` passes `fade_in + hold + fade_out`. `nullopt` when `enabled` is false or the document cannot be built. The engine does not call `show_splash` itself. `image_size` is the decoded pixel size of `config.image`.

## Inspector and profiler

The engine keeps the probes and the editor shows the panels. `set_inspector_attached` and `set_ui_profiler_attached` start them on a world; the snapshot functions (`inspector_tree`, `inspector_detail`, `inspector_rules`, `profiler_canvases`, `profiler_frames`, …) return plain data. Nothing binds a key, opens a window, or spawns a canvas, so a game cannot open them: the editor attaches them to the game world on Play ([Editor](../features/Editor.md)). The profiler functions compile to no-ops without `ENGINE_UI_PROFILER`. See the feature pages.

## Public headers

- `include/engine/ui/document.h`
- `include/engine/ui/element_id.h`
- `include/engine/ui/element_arena.h`
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
- `include/engine/ui/tree.h`
- `include/engine/ui/dock_layout.h`
- `include/engine/ui/dock_geometry.h`
- `include/engine/ui/dock_space.h`
- `include/engine/ui/inspector.h`
- `include/engine/ui/profiler.h`

## Tests

`tests/ui_safe_area_test.cpp` (safe insets per canvas fit, the stylesheet environment, `var(--safe-*)`), `tests/ui_element_id_test.cpp` (ids, parents, reuse, aliasing, stale lookups), `tests/ui_animation_tracks_test.cpp` (compiled keyframe tracks, motion layout chains), `tests/ui_picture_test.cpp` (pictures: quiet frames, one changed label, frame-only marks, `IPaint::version`), `tests/ui_list_windows_test.cpp` (variable-height windows, text cache, tree counts, camera culling), `tests/ui_dirty_flags_test.cpp` (quiet frames, one-string chain, relayout boundary, hover and layout, the layout checker), `tests/ui_bindable_version_test.cpp` (versions of `Bindable`, `BindableList`, `Catalog`), `tests/ui_wasted_work_test.cpp`, `tests/ui_bindable_visibility_test.cpp`, `tests/ui_slider_test.cpp`, `tests/ui_absolute_items_test.cpp`, `tests/ui_xml_test.cpp`, `tests/ui_builder_test.cpp`, `tests/ui_css_test.cpp`, `tests/ui_layout_hit_test.cpp`, `tests/ui_layout_dirty_gate_test.cpp`, `tests/ui_display_none_test.cpp`, `tests/ui_scroll_test.cpp`, `tests/ui_items_control_virtualization_test.cpp`, `tests/ui_tree_test.cpp`, `tests/dock_layout_test.cpp`, `tests/dock_space_test.cpp`, `tests/dock_float_window_test.cpp`, `tests/ui_text_wrap_test.cpp`, `tests/ui_text_input_test.cpp`, `tests/ui_label_select_test.cpp`, `tests/ui_input_batch_test.cpp`, `tests/ui_paint_binding_test.cpp`, `tests/ui_painter_test.cpp`, `tests/ui_refs_test.cpp`, `tests/ui_loc_test.cpp`, `tests/mvvm_test.cpp`, `tests/ui_inline_math_test.cpp`, `tests/ui_math_parser_test.cpp`, `tests/ui_math_layout_test.cpp`, `tests/ui_math_paint_test.cpp`, `tests/ui_math_font_test.cpp`, `tests/ui_math_stretch_test.cpp`, `tests/ui_math_element_test.cpp`, `tests/splash_test.cpp`, `tests/ui_inspector_test.cpp`, `tests/ui_profiler_test.cpp`, `tests/ui_popup_test.cpp`.

## See also

- [Localization](Localization.md)
- [Principles](../architecture/Principles.md)
