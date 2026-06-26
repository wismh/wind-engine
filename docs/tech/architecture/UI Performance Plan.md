# UI performance plan

This is a plan. It is not a description of the engine as it runs today. As-built behavior is [UI](../modules/UI.md).

## Already in the tree

These exist. They are not the numbered steps below, and the steps are not done.

- `ItemsControl` virtualization for a vertical list, one template root, and a fixed pixel row height. Step 6 is the extension (variable row height, collapsed trees, viewport culling).
- A layout dirty gate can skip `layout()` when text, custom properties, and generated owners are unchanged (`tests/ui_layout_dirty_gate_test.cpp`). Step 2 (a version on `Bindable`, three dirty flags, no full walk) is not that gate. `layout_state_changed` still walks the tree and still assigns the comparison copies.
- `assign_property_string` skips an equal string. The other step 1 items (no temporary pseudo-state vector, skipping a sibling sort, a motion overlay that does not copy a whole `ComputedStyle`) are not the current paint path.
- The UI profiler records the stages this plan uses as a baseline. See [UI Profiler](../features/UI%20Profiler.md).

None of steps 0 through 8 are closed. NanoVG is still the UI painter.

The live tree stays one `Element` tree: document, layout, and hit-test. A retained command list (`Picture`) is a second structure, produced by paint and consumed by the renderer. NanoVG is removed when that renderer lands. An offscreen texture is an explicit opt-in on top of the renderer, not the way retention works.

[Principles](Principles.md) stays in force: UI is document + style + view-model, UI â†’ game is `ICommand` / `IPaint`, engine APIs stay main-thread until a step below says otherwise. [Boundaries](Boundaries.md) stays in force: no `EventBus`. Dirty tracking is a version counter polled by the bind pass, not a subscription.

## Consumers

| Consumer | What hurts | What this plan has to make cheap |
| --- | --- | --- |
| Strategy game | HUD bound to a simulation that ticks every frame; unit lists; selection; a few always-on animations; a world inside a `Viewport` | One changed number does not walk or repaint the rest of the HUD. A list shows a window of rows. Camera pan does not rebuild the picture of the frame around the viewport. |
| Editor / UI-heavy tool | Inspector around 3k nodes; tables around 10k rows; trees that collapse | A quiet window does almost no CPU work. Scrolling a table shapes and lays out the visible window, not every row. Collapsed tree nodes are not in that window. |
| Application on the same engine | Forms, scrolling panels, text, mixed static chrome and live fields | Same pipeline as the editor. No second UI runtime. |
| Document swapped in later | Markup and style that arrive after the host is already up (download, generated XML) | One replacement of a slot's children, or of a whole `UiCanvas` document. Not a per-frame reconcile of a new description. |

Out of scope for every consumer:

- CSS fragmentation (columns, pages, floats, an inline element split across lines).
- A Flutter widget, element, and render-object stack.
- A React-style rebuild of the description every frame.
- Rich text as a second document model. If a product needs it, the runs live inside one `Label`, the same way an inline formula already does.
- A future `build()` that returns a description is a producer of `Element`s, like XML and `ui::Node`. It runs when that subtree's inputs change, not on every frame.

## Baseline

Before step 1, record three scenes with [UI Profiler](../features/UI%20Profiler.md):

1. Table, 10k rows.
2. Editor inspector, about 3k nodes.
3. Strategy HUD with at least one running animation and several bound numbers.

For each scene, on a Debug build with the game playing in the editor and its Profiler tab open, record:

- CPU ms of a quiet frame (nothing in the view-model changed, pointer still).
- CPU ms of a frame that changes one bound value.
- Draw-call count for that frame (`nvgEndFrame` path in `src/render/opengl/nanovg_painter.cpp` / `glnvg__renderFlush` in `external/nanovg/src/nanovg_gl.h`).

Every later step is closed by the same three numbers. After steps 2 and 4, a quiet frame is the target: no style match, no layout, no picture rebuild. Draw calls are the target of step 5, not of the earlier steps.

## Facts the steps rely on

- `Bindable` stores a value. `set` does not publish a change. `BindableList::get()` returns a mutable `std::vector&`, so a list can change with no signal (`include/engine/ui/bindable.h`).
- `run_bind` (`src/ecs/systems.cpp`) calls `apply_bindings` for every canvas with a view-model, every frame. That walks the tree (`src/ui/document.cpp` `bind_element`). The pointer path in `src/ui/canvas.cpp` can walk it again.
- `layout_state_changed` walks every element every time it is called and assigns `layout_dirty_check_text` and `layout_dirty_check_custom_properties` even when they compare equal.
- `children` and `generated_items` are `std::vector<Element>` by value. There is no parent link (`include/engine/ui/document.h`). An `Element*` and an index into that vector both die when the vector reallocates or splices.
- `style_cache_hits` (`src/ui/paint.cpp`) allocates `ancestor_pseudo_state_bits` on a hit for every non-root element. `compute_style` and `advance_tree` both call it.
- `child_stacking_order` (`src/ui/document.cpp`) builds a pointer vector and stable-sorts on every parent during paint and hit-test.
- While `motion_shown` is non-empty, paint copies the whole `ComputedStyle`. A finished `@keyframes` sample leaves `motion_shown` filled. `sample_prop` (`src/ui/style_anim.cpp`) rebuilds a `ComputedStyle` from raw declarations on each tick.
- `paint_element` calls `scissor` with the element rect after `apply_transform`. NanoVG stores color and scissor in per-call uniforms and issues one `glDrawArrays` per fill or text run.
- Per-element text measure and wrap caches already exist (`text_measure_cache_*`, `text_wrap_cache_*`). They are not shared across elements, and a recycled row does not keep a height except `virtualization_row_height_cache` (one float, fixed-px rows).
- `IPaint` runs from paint whenever it is bound. `Viewport` pan/zoom is a paint-time transform; hit-test inverts it. Neither participates in a dirty or clip model beyond the current full walk.

## Order

0 â†’ 1 â†’ 7a â†’ 2 â†’ 3 â†’ 6 â†’ 4 â†’ 5 â†’ 7b â†’ 8.

### 0 â€” Measurements

Capture the baseline above. Step 5 stays after step 4 unless those numbers show draw calls dominating a small tree; only then an in-frame batcher may jump ahead of `Picture`. The "most draws are solid rects, text, and images" assumption is checked here. If buttons are gradients, step 5's gradient path is on the hot set, not a side case.

### 1 â€” Stop the wasted work on the current tree

No new model. Makes the baseline readable.

- `ancestor_pseudo_state_bits` compares in place. No temporary `vector` on the hit path.
- `child_stacking_order` does not sort siblings whose `z-index` does not differ. Keep the shared function paint and hit-test already use.
- `layout_state_changed` assigns the text and `custom_properties` copies only when the comparison says they differ.
- A motion overlay writes the animated fields. It does not copy a whole `ComputedStyle`.
- A numeric binding does not format a string when the value is unchanged. `assign_property_string` already skips equal strings.

Done when the three scenes are faster on a quiet frame and a one-change frame, and the profiler still attributes Bind, Layout, Motion, and Paint separately.

### 7a â€” Stable identity and parent

Required before dirty flags can move without a full walk. Not the hot/cold split (that is 7b).

- Each element has a generational id in an arena. Id survives `ItemsControl` reconcile, which today move-assigns the `Element` value so caches and motion clocks stick.
- Each element stores its parent's id. `generated_owner` stays the data identity of a row. It is not the element id.
- An index into `vector<Element>` is not an id. A raw `Element*` is not an id.

Traversal, hit-test, and paint keep using the arena. Pointers held across a structural edit are illegal.

Done when a reconcile that reuses a row keeps the same id, a new row gets a new id, and a destroyed row's id does not alias a later row. Covered by `engine_tests` without a window.

### 2 â€” Versions, dirty flags, pseudo-class invalidation

Replaces both full walks: `apply_bindings` and `layout_state_changed`.

`Bindable` and `BindableList` gain a version. `set` bumps it only when the value changes. `BindableList`'s mutable `get()` goes away; mutation is `set` or `edit(fn)`, and both bump on a real change. This is a breaking change for games; update call sites. No listener list, no reverse pointer from `Bindable` to `Element`.

On document build, the engine fills a flat table of binding slots: element id, field, `BindingId`, last seen version. A quiet frame compares integers, one per slot. A mismatch reads the property and sets dirty flags on that element. Slots are rebuilt when the tree structure changes (7a), not every frame.

Three flags, independent: style, layout, paint. A flag bubbles to ancestors by parent id, and stops at a relayout boundary (parent size does not depend on the child) or a repaint boundary (step 4). Until step 4 exists, paint dirty still means "this subtree must be walked at paint."

Stylesheet parse records, per rule, two facts:

- Whether a pseudo-class sits on an ancestor combinator (`A:hover B`) or only on the subject.
- Which flags that rule dirties. Color and opacity are paint. Width, height, font, margin, padding, display are layout.

Hover, focus, and pressed then dirty the subject, plus descendants only where some rule's ancestor pseudo can match them. A color-only `:hover` does not layout.

`layout_state_changed`'s full-tree compare and the unconditional string/map assign go away once flags exist. `run_bind` still runs every frame; it scans the slot table.

Done when the quiet HUD and the quiet inspector do not match style and do not layout, including with the pointer resting on a control whose `:hover` rules are paint-only. A test counts style rebuilds and layout rebuilds per element for "no change", "one string changed", and "hover a paint-only control".

### 3 â€” Animation as tracks

`@keyframes` and `transition` are parsed into numeric tracks when the style is applied, not on each tick. `sample_prop` stops rebuilding a `ComputedStyle` from declaration strings.

`opacity`, `transform`, scroll, and `Viewport` camera do not set the layout flag. Width, height, font, padding, margin, gap do, and only on that chain (2's bubble rules). A finished keyframe holds its end value as the track's output. It does not keep a reason to copy the cascade.

Done when a steady opacity animation does not layout, and a width animation lays out only the chain whose size changed. Headless test: track sample equals the old string-sampled value on a fixture stylesheet.

### 6 â€” Lists, trees, grids

Immediately after 2. Does not need `Picture` or the new renderer. Uses the current painter's `measure_text` until step 5.

- Virtualization is a window over a stable row id (7a + `generated_owner`), not only the current `ItemsControl` case (vertical, one template root, fixed px height).
- Row height is an estimate until measured, then cached on the row id. Variable heights are legal. Recycling a row whose text, font, size, and width match the cache does not shape again. The existing per-element measure/wrap caches stay; this is the height stored beside the row, plus reuse of an identical `(text, font, size, width)` measure across rows.
- A collapsed tree node is not in the window. Its children are not bound or laid out. As built for fixed-height rows (wind-184): a tree is flat rows from `flatten_tree` in a virtualized `ItemsControl`, so a collapsed branch is not a row ([UI](../modules/UI.md#trees), `tests/ui_tree_test.cpp`).
- `Viewport` content that is a large world culls to the visible rect. Camera motion is a transform (step 4 will keep the surrounding picture). Culling is this step, because a strategy map cannot wait for the renderer.

6b, and it does not block step 4: a 2D window (rows Ã— columns) for tables and inspectors.

Done when the 10k-row scene binds and lays out a window plus overscan, and a collapsed branch adds nothing to those counts. Tests in `engine_tests` cover estimate â†’ measured height, recycle, collapse, and viewport culling with a fake painter.

### 4 â€” Retained Picture

A repaint boundary owns a `Picture`: canvas, scroll viewport, popup, or an element marked as a boundary. The picture is a list of engine commands (rounded rect, glyphs, image, gradient, arc, path, clip, layer opacity/transform). It is not a NanoVG command stream and not an offscreen texture.

Paint walks only paint-dirty subtrees and rewrites those pictures. A quiet boundary replays its list. `opacity`, `transform`, scroll, and `Viewport` camera change a parameter on the picture. They do not rewrite it.

`IPaint` joins this model. It exposes a version or calls `invalidate()`. Otherwise a graph either freezes or dirties its boundary every frame. The same rule as `Bindable`: no subscription bus.

`RecordingPainter` implements `IUiPainter` and records commands for `engine_tests`. No GL.

Scissor stays on `overflow` and `Viewport` only. Per-element scissor goes away here, before the renderer cares. Rotated clips still have to be expressible as commands (step 5 implements them).

Done when the quiet HUD replays pictures and the profiler paint scope does not walk their elements. One changed label rewrites that label's picture, not the screen. A test asserts the recorded command list is unchanged across two frames with no dirty flags.

### 5 â€” Renderer, NanoVG removed

The only GPU implementation of `Picture`. Playback may stay on the main thread. A second thread is legal only as a consumer of an immutable picture snapshot, and only after this step; the library still does not spawn threads (same split as a Noesis `Update` / `UpdateRenderTree`, client-driven).

Included, or NanoVG cannot be deleted:

- Rounded rects via SDF, color in the vertex. One draw for a run that shares a clip.
- Text quads from an atlas this renderer owns. `measure_text`, `break_lines`, and font metrics move here, including the layout path that step 6 still calls through the old painter.
- Images and nine-slice, one draw per atlas.
- Linear, radial, and conic gradients.
- Arcs.
- `fill_path` for formula outlines (`src/ui/math/math_paint.cpp`).
- Clips. An axis-aligned rect is a vertex attribute and a fragment discard, so it does not split a batch. A clip that is rotated with `apply_transform` is implemented in this renderer (stencil or a path), not left on `nvgScissor`.

`external/nanovg` and `src/render/opengl/nanovg_painter.cpp` leave in this step. GPU tests stay outside `engine_tests` ([Boundaries](Boundaries.md)). CPU coverage is the batch list: input commands â†’ batches, asserted in `engine_tests`.

Done when the three scenes match step 4's pictures without linking NanoVG, and the HUD draw-call count is the batch-list count from the test.

### 7b â€” Hot and cold element

After pictures exist, a quiet frame no longer walks style caches. Hit-test and a dirty frame still do.

The hot record is the tree link, parent id, `layout_rect`, and the dirty flags. Style-cache payload, caret, IME, formula cache, and scrollbar chrome move to a side block addressed by the same id. One lookup per visit, not a hash map per field.

Done when a hit-test walk touches the hot record, and a test that paints a quiet picture does not read the side block.

### 8 â€” Explicit offscreen target

A marked subtree may rasterize into a texture. The engine does not infer one. The mark is honored only when the picture is expensive and stable, or when the effect requires it (group opacity, blur, shadow). A subtree whose picture is dirty every frame ignores the mark and draws in the main target.

The texture is dropped on DPI or window-size change. A memory budget refuses a new texture past the cap.

Done when a marked static panel is one quad on a quiet frame, an animated counter inside an unmarked label does not allocate a texture, and a test of the budget rejects the next mark without GL.

## Later, not a step

Layout as jobs is fork-join inside one parent: `layout_stack` already measures every flow child from a known `child_basis` before it places them (`src/ui/document.cpp`). Children do not read each other's sizes, so those measures can be jobs; placement stays on the join. A job must not call the painter. That only becomes true after step 5, if measure is thread-safe and does not touch the glyph atlas from two jobs. Grain is a dirty row or other dirty subtree, not every element. A deep stack still waits on its children.

## Tests

Each step lands with `engine_tests` and no GL. The counters and the `RecordingPainter` are the contract that a quiet frame stayed quiet. GPU checks for the renderer and for offscreen textures stay out of `engine_tests`, next to the other windowed backends.

## See also

- [UI](../modules/UI.md)
- [UI Profiler](../features/UI%20Profiler.md)
- [UI Markup](../features/UI%20Markup.md)
- [Principles](Principles.md)
- [Boundaries](Boundaries.md)
- `include/engine/ui/document.h`
- `include/engine/ui/bindable.h`
