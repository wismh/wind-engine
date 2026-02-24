# UI performance plan

This is a plan. It is not a description of the engine as it runs today. As-built behavior is [UI](../modules/UI.md).

## Already in the tree

These exist. They are not the numbered steps below, and the steps are not done.

- `ItemsControl` virtualization for a vertical list, one template root, and a fixed pixel row height. Step 6 is the extension (variable row height, collapsed trees, viewport culling).
- Before step 2 a layout dirty gate (`layout_state_changed`) skipped `layout()` by walking the tree. Step 2 replaced it with dirty flags and `sync_layout` (`tests/ui_layout_dirty_gate_test.cpp` now tests that layout is skipped when nothing is marked).
- `assign_property_string` skips an equal string. The other step 1 items (no temporary pseudo-state vector, skipping a sibling sort, a motion overlay that does not copy a whole `ComputedStyle`, entering no painter state for an element with nothing to set) landed in wind-226; see step 1.
- The UI profiler records the stages, the draw calls, and the painter calls by kind this plan uses as a baseline. See [UI Profiler](../features/UI%20Profiler.md).

Steps 0, 1, 7a, 2, 3, 6, and 4 are closed (see their sections). Steps 5, 7b, and 8 are not. NanoVG is still the UI painter.

The live tree stays one `Element` tree: document, layout, and hit-test. A retained command list (`Picture`) is a second structure, produced by paint and consumed by the renderer. NanoVG is removed when that renderer lands. An offscreen texture is an explicit opt-in on top of the renderer, not the way retention works.

[Principles](Principles.md) stays in force: UI is document + style + view-model, UI → game is `ICommand` / `IPaint`, engine APIs stay main-thread until a step below says otherwise. [Boundaries](Boundaries.md) stays in force: no `EventBus`. Dirty tracking is a version counter polled by the bind pass, not a subscription.

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

The scenes are `table`, `inspector`, and `hud` of `wind_ui_bench` ([UI Bench](../features/UI%20Bench.md)), measured in RelWithDebInfo, the configuration the reference numbers come from. For each scene record:

- CPU ms of a quiet frame (nothing in the view-model changed, pointer still): mode `quiet`.
- CPU ms of a frame that changes one bound value: mode `one-change`.
- Draw-call count for that frame: `draw_calls` of each canvas ([UI Profiler](../features/UI%20Profiler.md#counters)), the `glDrawArrays` of `glnvg__renderFlush` in `external/nanovg/src/nanovg_gl.h`.

Recorded in [bench/results/baseline/summary.md](../../../bench/results/baseline/summary.md) (all 34 rows of the matrix, `-Repeat 3`, medians; RelWithDebInfo, 1600 x 900, vsync off, Ryzen 5 5600H laptop; taken on an uncommitted tree, so its commit is `e7211f3` with `dirty` true). CPU ms per frame, `total` = every stage plus `commands`:

| Scene | Elements | Quiet total | Quiet paint | One-change total | One-change layout | One-change paint | Draw calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `table` | 244 | 0.44 | 0.35 | 0.67 | 0.17 | 0.39 | 278 |
| `inspector` | 2,926 | 3.20 | 1.78 | 5.07 | 1.82 | 1.77 | 1,707 |
| `hud` | 184 | 0.62 | 0.53 | 0.76 | 0.15 | 0.53 | 1,995 |

A quiet frame skips layout in all three. The rest of a quiet frame is bindings (0.75 ms in `inspector`), motion, and paint. Sub-millisecond rows move by up to about 15% between runs ([UI Bench](../features/UI%20Bench.md#repeatability)).

The Windows table is the owner's reference machine. The numbers below are the Linux baseline of the overnight work, the one every step's before/after table compares against ([bench/results/baseline-linux/summary.md](../../../bench/results/baseline-linux/summary.md), all 34 rows, `-Repeat 5`, medians). Machine: a 4-thread Intel Xeon @ 2.10 GHz shared VM, Ubuntu 24.04, Mesa software GL (llvmpipe) under `xvfb-run`, RelWithDebInfo (GCC), 1600 x 900, vsync off, commit `4f4af64` (clean tree). **Read only the CPU stages Bind, Style, Layout, and Motion as comparable with Windows. Paint includes NanoVG on llvmpipe, so it is relative: compare it with itself from step to step, not with the Windows column.** The VM is shared and noisy: the `spread` column of the summary is 5 to 40% on sub-millisecond rows, so a change under about 20% there is noise. Counters (draw calls, painter calls, element counts, layout `skip`) are exact and repeat.

| Scene | Elements | Quiet total | Quiet paint | One-change total | One-change layout | One-change paint | Draw calls | State calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `table` | 244 | 0.38 | 0.28 | 0.48 | 0.12 | 0.27 | 278 | 1,144 |
| `inspector` | 2,926 | 1.84 | 1.16 | 3.26 | 1.42 | 1.15 | 1,707 | 4,999 |
| `hud` | 184 | 0.52 | 0.43 | 0.60 | 0.13 | 0.40 | 1,995 | 873 |

Same counters as Windows (278, 1,707, and 1,995 draw calls; 244, 2,926, and 184 elements), so the scenes are the same scenes. A quiet frame skips layout in all three here too, and the `hover layout` rows run layout on no frame (the fact the plan notes under pseudo-classes). Quiet bindings: `table` 0.054 ms, `inspector` 0.28 ms, `hud` 0.031 ms. The outliers hold: `paint-mix/conic-gradient` 5.9 ms of paint against 0.5 for the other box kinds, `math` 7.9 ms, `nine-slice` 2.6 ms.

To repeat it on Linux: [UI Bench](../features/UI%20Bench.md#linux). The scripts run under PowerShell 7 (`pwsh`) too, so one tool serves both systems.

Every later step is closed by re-running the matrix (`bench/run_matrix.ps1 -Repeat 3` on Windows) and `bench/compare.ps1 -Base bench/results/baseline -Head <run>`, the same three numbers per scene. The overnight steps compare against `bench/results/baseline-linux` with `-Repeat 5`. After steps 2 and 4, a quiet frame is the target: no style match, no layout, no picture rebuild. Draw calls are the target of step 5, not of the earlier steps.

## Facts the steps rely on

- (Step 2 fixed) `Bindable` has a `version`, `set` bumps it on a real change and `BindableList::get()` is const; a list changes through `set` or `edit`.
- (Step 2 fixed) `run_bind` (`src/ecs/systems.cpp`) still calls `apply_bindings` for every canvas with a view-model every frame, but a quiet frame polls the flat slot table (`src/ui/bind.cpp`) and walks no tree. The pointer path in `src/ui/canvas.cpp` marks what it edits.
- (Step 2 fixed) `layout_state_changed` is gone; `sync_layout` (`src/ui/painter.h`) lays out only marked chains.
- `children` and `generated_items` are `std::vector<Element>` by value (`include/engine/ui/document.h`). An `Element*` and an index into that vector both die when the vector reallocates or splices. Until step 7a (wind-227) there was no parent link and no element id; since then every element carries `handle` and `parent` (see 7a). The inspector's selection path and a text-selection gesture still re-resolve a child-index path on every use.
- (Step 1 fixed) `style_cache_hits` (`src/ui/paint.cpp`) allocated `ancestor_pseudo_state_bits` on a hit for every non-root element; it now compares in place. `compute_style` and `advance_tree` both call it.
- `child_stacking_order` (`src/ui/document.cpp`) builds a pointer vector for every parent during paint and hit-test. Since step 1 it stable-sorts only siblings whose `z-index` does not already ascend.
- While `motion_shown` is non-empty, paint overlays the held sample on a style that carries only the fields `paint_element` reads (step 1; it used to copy the whole `ComputedStyle`). A finished `@keyframes` sample leaves `motion_shown` filled. `sample_prop` (`src/ui/style_anim.cpp`) rebuilds a `ComputedStyle` from raw declarations on each tick.
- `paint_element` calls `scissor` with the element rect after `apply_transform`. NanoVG stores color and scissor in per-call uniforms and issues one `glDrawArrays` per fill or text run.
- Per-element text measure and wrap caches already exist (`text_measure_cache_*`, `text_wrap_cache_*`). They are not shared across elements, and a recycled row does not keep a height except `virtualization_row_height_cache` (one float, fixed-px rows).
- (Step 2 changed) Layout used to resolve style without pseudo-classes. It now reads the real pseudo-state for a rule that has a pseudo-class on its subject or an ancestor combinator and sets a layout property; every other rule keeps the paint-only path. `.hot:hover { padding }` therefore relayouts on enter and leave (owner decision; a behaviour change for existing sheets). See [UI](../modules/UI.md#hover-and-layout).
- Every painted element cost painter state calls every frame (fixed in step 1, which keeps the scissor where it can clip something; see there). `paint_element`'s `enter` (`src/ui/paint.cpp`) called `save`, `set_opacity`, and `scissor` with the element rect, and the element ended with `restore`, whether or not it drew anything or had opacity below 1; every element clipped itself and its in-flow children to its own box, not only `overflow` elements, and `set_opacity` replaces the painter's alpha instead of multiplying it; an absolute child of a non-clipping parent adds one more `restore`/`enter`. Only an item template, or an invisible or `display: none` element and its subtree, returns before `enter`. The baseline histograms show about one of each per painted element: `table` 238 `save` and `scissor` for 244 elements, `inspector` 1,161 of 2,926 (the rest are not painted), `hud` 189 `save` and 184 `scissor` for 184. NanoVG keeps each `scissor` as per-call uniform state. Relevant to step 1 (an element with opacity 1, no transform, and no clip need not enter at all), step 4 (per-element scissor goes away), and step 5 (a clip must not split a batch).
- `IPaint` runs from paint whenever it is bound. `Viewport` pan/zoom is a paint-time transform; hit-test inverts it. Neither participates in a dirty or clip model beyond the current full walk.

## Order

0 → 1 → 7a → 2 → 3 → 6 → 4 → 5 → 7b → 8.

### 0 — Measurements

As built (wind-225): the Linux baseline above, a `linux-bench` preset, and the matrix scripts running under PowerShell 7. Capture the baseline above. Step 5 stays after step 4 unless those numbers show draw calls dominating a small tree; only then an in-frame batcher may jump ahead of `Picture`. The "most draws are solid rects, text, and images" assumption is checked here. If buttons are gradients, step 5's gradient path is on the hot set, not a side case.

Baseline answer (painter calls per frame, [baseline summary](../../../bench/results/baseline/summary.md)):

| Scene | Drawing calls |
| --- | --- |
| `table` | 193 `text`, 40 `fill_rect`, 2 `fill_rounded_rect` |
| `inspector` | 356 `text`, 258 `fill_rect`, 246 `fill_rounded_rect`, 29 `stroke_rect` |
| `hud` | 808 `fill_rect` (mostly the `IPaint` world grid and minimap), 123 `text`, 64 `line`, 12 `arc`, 8 `fill_rounded_rect`, 3 `stroke_rect`, 1 `linear_gradient` |

Most draws are solid rects (square or rounded) and text. The reference scenes draw no image or nine-slice, and one gradient in all three, so gradients are not on the hot set; images are absent because these scenes have none, not because they are cheap. Draw calls follow the rect count, not the tree: `hud` has the fewest elements and the most draw calls (1,995), almost all from its `IPaint`s. Outliers in `paint-mix` (1,000 cells each; linear, radial, solid, image, and border all paint in about 0.85 ms): `conic_gradient` paints in 13.8 ms with the same 2 draws a cell, so its cost is CPU, not draw calls: every call looks its baked texture up by a string key built with a `std::to_string` per angle and stop component (`gradient_cache_key` / `ensure_conic_texture`, `src/render/opengl/nanovg_painter.cpp`), although every cell shares one texture; `<Math>` (`path`) paints in 10.3 ms with 9 draws a formula; `nine_slice` 3.5 ms and 18 draws a cell.

### 1 — Stop the wasted work on the current tree

No new model. Makes the baseline readable.

- `ancestor_pseudo_state_bits` compares in place. No temporary `vector` on the hit path.
- `child_stacking_order` does not sort siblings whose `z-index` does not differ. Keep the shared function paint and hit-test already use.
- `layout_state_changed` assigns the text and `custom_properties` copies only when the comparison says they differ.
- A motion overlay writes the animated fields. It does not copy a whole `ComputedStyle`.
- A numeric binding does not format a string when the value is unchanged. `assign_property_string` already skips equal strings.

Done when the three scenes are faster on a quiet frame and a one-change frame, and the profiler still attributes Bind, Layout, Motion, and Paint separately.

As built (wind-226). All six items, the conic key too. What changed and what it did not:

- `style_cache_hits` compares the ancestors' pseudo-state bytes in place. `child_stacking_order` skips the sort for siblings that already ascend. `layout_state_changed` still visits every element every call (step 2 removes the walk) but assigns the text, the custom-property map, and the owner list only when they differ.
- A held motion sample is overlaid on a style built from the fields `paint_element` reads (`copy_paint_fields`), not a copy of the cascade.
- `enter` is skipped for an element that has nothing to set: the opacity it needs is the one the painter already holds, no transform, and a scissor that would clip nothing (`paint_stays_in_box`: no border, no `Line`/`Math`/`TextInput`/`Viewport`/`ScrollView`/`IPaint`/`overflow`, measured text fits the content box, in-flow children inside the box with no transform). The plan said "no clip", but the old paint clipped every element to its own box, so the skip is conditional on the clip being redundant. A border keeps it because NanoVG strokes centre the line on the edge and the scissor trimmed the outer half. `tests/ui_wasted_work_test.cpp` has an oracle: every fill of a fixture is clipped as when every element scissors. 15 bench rows compared by screenshot before and after (Xvfb, software GL): identical except at most 151 anti-aliasing pixels in `inspector` (a glyph at a box edge no longer trimmed). Opacity is not inherited here (`set_opacity` replaces), so a child with its own opacity 1 under a parent at 0.5 still sets 1.
- The conic texture cache is a short list searched by content (floats at 1e-6, the resolution of the old text key), not a `std::to_string` key per call.

Linux baseline against step 1 (`-Repeat 5`, medians; counters are exact, times carry the noise caveat of the baseline):

| Scene | State calls | Quiet total ms | One-change total ms | Draw calls |
| --- | ---: | ---: | ---: | ---: |
| `table` | 1,144 -> 203 | 0.38 -> 0.38 | 0.48 -> 0.51 | 278 -> 278 |
| `inspector` | 4,999 -> 482 | 1.84 -> 1.58 | 3.26 -> 3.06 | 1,707 -> 1,707 |
| `hud` | 873 -> 183 | 0.52 -> 0.49 | 0.60 -> 0.59 | 1,995 -> 1,995 |

`paint-mix/conic-gradient` 6.09 -> 0.92 ms. No row regressed past 20%. Profiler stages are still separate; layout still skips on a quiet frame.

### 7a — Stable identity and parent

Required before dirty flags can move without a full walk. Not the hot/cold split (that is 7b).

- Each element has a generational id in an arena. Id survives `ItemsControl` reconcile, which today move-assigns the `Element` value so caches and motion clocks stick.
- Each element stores its parent's id. `generated_owner` stays the data identity of a row. It is not the element id.
- An index into `vector<Element>` is not an id. A raw `Element*` is not an id.

Traversal, hit-test, and paint keep using the arena. Pointers held across a structural edit are illegal.

Done when a reconcile that reuses a row keeps the same id, a new row gets a new id, and a destroyed row's id does not alias a later row. Covered by `engine_tests` without a window.

As built (wind-227). The arena is a slot table, not the storage of the elements: `children` and `generated_items` stay `std::vector<Element>` by value, so the ~440 call sites that walk them (engine, editor, tests) are untouched, and nothing about traversal, hit-test, or paint changed (the bench element counts are the same). What is new:

- `ElementId` (`index`, `generation`; the null id names nothing) in `include/engine/ui/element_id.h`, `ElementArena` in `element_arena.h`, one per `UiDocument`. A slot stores the parent's id, the position in the parent's `children` or `generated_items`, and which list, never an address. `UiDocument::find(id)` climbs parent slots, walks the positions down with bounds checks, and requires the element it reaches to carry the id: a stale id, a null id, and a tree edited without `reindex()` are `ElementLookupError::Stale`, `Null`, `Misplaced`, named by `element_lookup_error_name`, not a pointer to the wrong element or freed memory. Because a slot holds no address, a copied or moved document keeps valid ids and a vector may relocate its elements freely. The alternative that stores `Element*` in the slots needs a fix-up on every relocation and is UB when a caller forgets.
- `Element::handle` and `Element::parent`. `parse_xml` and `make_document` call `UiDocument::reindex()`; `apply_bindings` does for a hand-built tree. The ItemsControl reconcile keeps a reused row's id (and so its clocks and caches), gives a cloned row and its subtree new ids (the clone copied the template's), frees the ids of every dropped row's subtree, and rewrites every row's parent and position. A freed slot comes back on a new generation, so a dead id never names a later row (a test walks a 200-row window down and up and checks no id is handed out twice).
- `generated_owner` is the data identity and unchanged.
- Held pointers replaced: the focus (`UiFocus::element`, which dangled when the focused row left the window), a scrollbar drag (`ActiveScrollbarDrag::element`, was a child-index path), and the ancestor chain of the style cache (`StyleCacheEntry::ancestors`, was an address, so a relocated vector could alias a different element). An ancestor with no id never matches, so a hand-built tree restyles every frame; every parsed or built document has ids.

`tests/ui_element_id_test.cpp`: ids and parents after parse and builder, reuse and new rows, no aliasing over a long scroll, parents after reconcile and splice, stale and misplaced lookups with their names, a copied and moved document, a hand-copied element, `apply_bindings` on a hand-built tree. No window.

Against step 1 (`-Repeat 3` on the rows below, medians; element and generated counts are exact): `table` 244/38, `inspector` 2,926/395, `hud` 184/32 elements/generated, unchanged; draw calls unchanged. Quiet total ms `table` 0.38 -> 0.39, `hud` 0.49 -> 0.47, `inspector` 1.58 -> 1.78 (bindings 0.25 -> 0.29; inside the noise of a shared VM, re-measured with step 2).

### 2 — Versions, dirty flags, pseudo-class invalidation

Replaces both full walks: `apply_bindings` and `layout_state_changed`. Owner decision for this step: `:hover`, `:focus`, and `:active` may change layout.

`Bindable` and `BindableList` gain a version. `set` bumps it only when the value changes. `BindableList`'s mutable `get()` goes away; mutation is `set` or `edit(fn)`, and both bump on a real change. This is a breaking change for games; update call sites. No listener list, no reverse pointer from `Bindable` to `Element`.

On document build, the engine fills a flat table of binding slots: element id, field, `BindingId`, last seen version. A quiet frame compares integers, one per slot. A mismatch reads the property and sets dirty flags on that element. Slots are rebuilt when the tree structure changes (7a), not every frame.

Three flags, independent: style, layout, paint. A flag bubbles to ancestors by parent id, and stops at a relayout boundary (parent size does not depend on the child) or a repaint boundary (step 4). Until step 4 exists, paint dirty still means "this subtree must be walked at paint."

Stylesheet parse records, per rule, two facts:

- Whether a pseudo-class sits on an ancestor combinator (`A:hover B`) or only on the subject.
- Which flags that rule dirties. Color and opacity are paint. Width, height, font, margin, padding, display are layout.

Hover, focus, and pressed then dirty the subject, plus descendants only where some rule's ancestor pseudo can match them. A color-only `:hover` does not layout.

`layout_state_changed`'s full-tree compare and the unconditional string/map assign go away once flags exist. `run_bind` still runs every frame; it scans the slot table.

Done when the quiet HUD and the quiet inspector do not match style and do not layout, including with the pointer resting on a control whose `:hover` rules are paint-only. A test counts style rebuilds and layout rebuilds per element for "no change", "one string changed", and "hover a paint-only control".

As built (wind-228). Every item, with these shapes:

- Versions: `Bindable::version`, `BindableList::set` / `edit` (no mutable `get()`; migrate `list.get().push_back(x)` to `list.edit([&](auto &items) { items.push_back(x); })`), `ViewModel::property_version`, `loc::Catalog::version`. An equal value or equal list bumps nothing.
- Bind table (`src/ui/bind.cpp`): `bind_element` builds the full tree once (and after a structural change) and records a `BindSlot` per binding (element id, field, binding id, last version). A quiet frame compares integers; a mismatch reads the value and calls `mark_dirty`. Commands and ItemsControl/scroll windows are refreshed on their own triggers (`can_execute` is polled, windows after layout).
- Flags (`src/ui/dirty.cpp`): `dirty_self` / `dirty_below` for style, layout, paint, bubbled by parent id up to a relayout boundary (a fixed-size `px` width and height, not hugging). `sync_layout` restyles only dirty elements, diffs the layout fields (`LayoutFields`), and lays out from the marked roots; it replaces `layout_state_changed` in the prepare path and in `paint_document`.
- Stylesheet analysis (`style_analysis`, `CssRule::subject_pseudo` / `ancestor_pseudo` / `dirties` / `line`): style cache keys compare pseudo-state only through the dependency masks of rules that could match; hover dirties the subject and, only where an ancestor-pseudo rule can match, descendants. Layout reads real pseudo-state only for rules that set a layout property under a pseudo-class.
- Debug: a one-time warning per (rule, document) for a layout property under a pseudo-class in a tree over 500 elements, with document and line; `ENGINE_UI_LAYOUT_CHECK` (Debug always, otherwise opt-in) compares every `sync_layout` with a from-scratch copy and ends the process on a difference.
- Found by the checker and fixed: a subtree shown again after being hidden (`layout_hidden` forces a full relayout of it), a boundary-stopped mark that was never cleared.
- Facts corrected above: the layout gate and `apply_bindings` walk no longer exist; layout is no longer pseudo-blind.
- Profiler JSON gains `layout_frames` and `work` counters; the bench summary shows how many measured frames ran layout.

Tests: `tests/ui_dirty_flags_test.cpp` (quiet frame counters, one changed string, relayout boundary, paint-only hover, layout hover on enter and leave only, ancestor pseudo, `if`, command poll, warning, checker, every pseudo class, shown again), `tests/ui_bindable_version_test.cpp`. No window.

Linux baseline against step 2 (`-Repeat 5`, medians, same machine; the hover rows move the pointer between `hot` elements, so enter or leave happens on every measured frame and `hover / layout` lays out on 120 of 120 frames, `hover / paint` on 0):

| Row | Layout ms | Total ms |
| --- | ---: | ---: |
| `table / one-change` | 0.115 -> 0.011 | 0.484 -> 0.378 |
| `inspector / one-change` | 1.417 -> 0.012 | 3.261 -> 1.728 |
| `hud / one-change` | 0.125 -> 0.021 | 0.601 -> 0.537 |
| `inspector / quiet` | 0.000 -> 0.001 | 1.839 -> 1.720 |
| `table / quiet` | 0.000 -> 0.001 | 0.382 -> 0.353 |
| `hud / quiet` | 0.000 -> 0.001 | 0.516 -> 0.493 |
| `table / hover / paint` | 0.000 -> 0.001 | 0.494 -> 0.361 |
| `inspector / hover / paint` | 0.000 -> 0.001 | 2.495 -> 1.802 |
| `table / hover / layout` | 0.000 -> 0.054 | 0.497 -> 0.472 |
| `inspector / hover / layout` | 0.000 -> 0.320 | 2.335 -> 2.238 |
| `inspector / churn` | 1.438 -> 0.868 | 3.408 -> 2.789 |
| `inspector / scroll` | 0.405 -> 0.243 | 2.719 -> 2.258 |

`hover / layout` rows now do the layout work the owner asked for (before they skipped it by being pseudo-blind). `compare.ps1` flags one row, `clip / quiet` (0.87 -> 0.91 ms, +5.3%, paint only, layout and draw calls unchanged), which is inside the noise of the VM. Bind time of a quiet inspector frame, from the counters, 0.28 -> about 0.1 ms. Unverified: Windows numbers.

### 3 — Animation as tracks

`@keyframes` and `transition` are parsed into numeric tracks when the style is applied, not on each tick. `sample_prop` stops rebuilding a `ComputedStyle` from declaration strings.

`opacity`, `transform`, scroll, and `Viewport` camera do not set the layout flag. Width, height, font, padding, margin, gap do, and only on that chain (2's bubble rules). A finished keyframe holds its end value as the track's output. It does not keep a reason to copy the cascade.

Done when a steady opacity animation does not layout, and a width animation lays out only the chain whose size changed. Headless test: track sample equals the old string-sampled value on a fixture stylesheet.

As built (wind-229). `@keyframes` compile on first sample to `CompiledKeyframes` (one track per animated property, stops sorted, `Keyframes::compiled`, rebuilt when the stops or their declarations changed), so `advance_motion` parses no declaration string; the previous string-sampled function stays as `sample_keyframes_from_declarations` for the test that compares both. Transitions did not parse strings per tick and are unchanged. The motion pass no longer lays out the whole tree: an element whose sample moved a layout field is marked for restyle, the restyle commits the sample and marks layout only if a field moved, and `sync_layout` lays out that chain (through relayout boundaries). `layout_descendant_inputs_changed` is gone. A finished track holds its end value without costing a cascade and without keeping the hover search alive (`motion_active` is true only while a clock runs or a sample moved in the frame). Facts corrected: the old plan text claimed `sample_prop` rebuilt a `ComputedStyle` per declaration string; it parsed `CssDeclaration` strings per stop per property per tick, which is what the tracks remove.

Tests: `tests/ui_animation_tracks_test.cpp` (track sample against the declaration-parsed sample for opacity, width, padding, background at 17 progress values; tracks built once and rebuilt on edit; a steady opacity animation lays out nothing; a width animation lays out only its chain; a finished animation costs no cascade). Run as well under the layout checker.

Against step 2 (`-Repeat 5`, medians, same machine; other rows moved by less than the noise of the VM):

| Row | Layout ms | Total ms |
| --- | ---: | ---: |
| `motion / quiet / paint-props` | 0.001 -> 0.001 | 1.154 -> 0.408 |
| `motion / quiet / layout-props` | 0.044 -> 0.031 | 0.363 -> 0.299 |
| `motion / quiet / both` | 0.047 -> 0.034 | 1.167 -> 0.535 |

`compare.ps1` flags five other rows between +5% and +12% (`table / quiet`, `table / hover / paint`, `inspector / one-change`, `hud / hover / paint`, `paint-mix / linear-gradient`) with identical counters and no code change on those paths: noise of the shared VM, not a result. Unverified: Windows numbers.

### 6 — Lists, trees, grids

Immediately after 2. Does not need `Picture` or the new renderer. Uses the current painter's `measure_text` until step 5.

- Virtualization is a window over a stable row id (7a + `generated_owner`), not only the current `ItemsControl` case (vertical, one template root, fixed px height).
- Row height is an estimate until measured, then cached on the row id. Variable heights are legal. Recycling a row whose text, font, size, and width match the cache does not shape again. The existing per-element measure/wrap caches stay; this is the height stored beside the row, plus reuse of an identical `(text, font, size, width)` measure across rows.
- A collapsed tree node is not in the window. Its children are not bound or laid out. As built for fixed-height rows (wind-184): a tree is flat rows from `flatten_tree` in a virtualized `ItemsControl`, so a collapsed branch is not a row ([UI](../modules/UI.md#trees), `tests/ui_tree_test.cpp`).
- `Viewport` content that is a large world culls to the visible rect. Camera motion is a transform (step 4 will keep the surrounding picture). Culling is this step, because a strategy map cannot wait for the renderer.

6b, and it does not block step 4: a 2D window (rows × columns) for tables and inspectors.

Done when the 10k-row scene binds and lays out a window plus overscan, and a collapsed branch adds nothing to those counts. Tests in `engine_tests` cover estimate → measured height, recycle, collapse, and viewport culling with a fake painter.

As built (wind-230). Four of the five items; 6b (a 2D window over rows and columns) is not built, as the plan allows.

- Rows of no fixed height are windowed. The window search is over `RowHeights::tops` (prefix sums of the heights measured into `Element::row_heights`, keyed by `generated_owner`), an item never measured counts as the mean of the measured, and `scroll_item_into_view` uses the same tops ([UI](../modules/UI.md#rows-of-no-fixed-height)). The first bind of any list still builds every row (no layout, no scroller size yet), so a 10k-row list pays one full build and layout, then binds and lays out a window plus overscan (the test counts 10,000 rows at the first frame and fewer than 30 two frames later, under 120 elements in the document).
- A document-wide text cache (`UiDocument::text_cache`) behind the per-element measure and wrap caches: rows with the same text, font and size reach the painter once, and a row scrolled back does not shape again (tests with a counting painter). Facts corrected: wrapping did re-break on a line-height change before; it no longer does, because the break does not depend on the stride (`UiTextWrapLayout.LineHeightChangeKeepsTheBreaksAndTakesTheNewStride`).
- A collapsed tree branch: already true for fixed rows (wind-184, flat rows from `flatten_tree`); the new test counts a 5,000-child branch collapsed against expanded (10 rows against a window), and the variable-height path serves trees whose rows wrap.
- A `Viewport` camera culls children (at any depth until a scroll, transform or camera changes the space) that lie wholly outside the visible world rect; an element whose reach is unknown is never culled; a world drawn by `IPaint` is the game's to cull. Counted in `Element::culled_children`; hit-testing is not culled.

Not done, said plainly: no 10k-row scene was added to the bench matrix (the numbers below come from the existing rows), the scroll offset is not anchored when a row above the window measures differently than its estimate, and heights of rows outside the window are not remeasured on a width change.

Against step 3 (`-Repeat 5`, medians, same machine). The inspector rows moved because its scenes now window: its history list has rows of no fixed height, so the scene builds 316 elements where it built 2,926 (the scene is a different shape from the baseline's; draw calls fall with it, 1,707 -> 315). That is the feature working, not a like-for-like speed-up of the same tree.

| Row | Elements | Layout ms | Total ms | Draw calls |
| --- | ---: | ---: | ---: | ---: |
| `inspector / quiet` | 2,926 -> 316 | 0.001 -> 0.001 | 1.621 -> 0.317 | 1,707 -> 315 |
| `inspector / one-change` | 2,926 -> 316 | 0.013 -> 0.015 | 1.817 -> 0.318 | 1,707 -> 315 |
| `inspector / scroll` | 2,928 -> 421 | 0.238 -> 0.054 | 2.284 -> 0.547 | 1,717 -> 351 |
| `inspector / churn` | 2,926 -> 316 | 0.892 -> 0.401 | 2.924 -> 1.411 | 1,674 -> 426 |
| `table / scroll` | 244 -> 257 | 0.042 -> 0.045 | 0.547 -> 0.585 | 297 -> 297 |

`compare.ps1` flags 16 rows, all with unchanged counters and none on a path this step touched (`paint-mix`, `motion`, `hud`, `table`): a drift of 5-15% across the whole matrix, the same on rows that paint only. It is not isolated from the noise of the shared VM; unverified, to be re-measured on another machine. Unverified: Windows numbers.

### 4 — Retained Picture

A repaint boundary owns a `Picture`: canvas, scroll viewport, popup, or an element marked as a boundary. The picture is a list of engine commands (rounded rect, glyphs, image, gradient, arc, path, clip, layer opacity/transform). It is not a NanoVG command stream and not an offscreen texture.

Paint walks only paint-dirty subtrees and rewrites those pictures. A quiet boundary replays its list. `opacity`, `transform`, scroll, and `Viewport` camera change a parameter on the picture. They do not rewrite it.

`IPaint` joins this model. It exposes a version or calls `invalidate()`. Otherwise a graph either freezes or dirties its boundary every frame. The same rule as `Bindable`: no subscription bus.

`RecordingPainter` implements `IUiPainter` and records commands for `engine_tests`. No GL.

Scissor stays on `overflow` and `Viewport` only. Per-element scissor goes away here, before the renderer cares. Rotated clips still have to be expressible as commands (step 5 implements them).

Done when the quiet HUD replays pictures and the profiler paint scope does not walk their elements. One changed label rewrites that label's picture, not the screen. A test asserts the recorded command list is unchanged across two frames with no dirty flags.

As built (wind-231). All items. Shape: `Picture` (`src/ui/picture.h`) is a list of `IUiPainter` calls (a `std::variant` per call, `Call` for a nested boundary's picture), `RecordingPainter` writes one without a GPU, and every painter replays one (the NanoVG painter included: it replays calls, NanoVG stays the painter until step 5). Boundaries are the root, anything that scrolls, a `Viewport`, a `Popup` and `repaint-boundary: yes`. A boundary owns `chrome` (its own drawing), `content` (its children) and `frame` (the state it sets itself, the calls of the other two, the scrollbar); opacity, transform, scroll and the camera live in the frame and `kDirtyFrame` rewrites only it. `IPaint::version()` (default `kVersionUnknown`, drawn every frame) lets a paint be replayed; a focused text input and a measure of zero width are volatile. A `Viewport` records half a screen of world more than it shows. Details: [UI](../modules/UI.md#pictures).

What the plan text got wrong or left open, now settled: "scissor stays on `overflow` and `Viewport`, per-element scissor goes away" is not done as written: after step 1 an element sets a scissor only when its own drawing can leave its box (text wider than the box, a border, a formula), and that stays, because dropping it would draw text that overflowed a button outside it. The paint marks were never needed while paint walked everything, so several paths wrote state without one (text selection, focus, scroll into view, scrollbar hover); they mark now, and the picture check (below) is what finds the next one. `scroll_item_into_view` takes the document (breaking, one call site in the editor).

Check: a build with `ENGINE_UI_LAYOUT_CHECK` records every boundary afresh after each paint and compares it with what it holds; the whole suite and all 34 bench rows pass it.

Tests: `tests/ui_picture_test.cpp`: replay equals the direct calls, a quiet frame replays and paints no element (`painted_elements` and `pictures_recorded` do not move, the command lists are equal), one changed label rewrites only its boundary, a text change inside a plain region rewrites its boundary, a resized canvas records again, scrolling rewrites no picture, an `IPaint` with a version is drawn again only when it moves and one without every frame, a focused text input keeps blinking, a copied element has no picture, a camera pan inside the recorded world replays.

Bench (`-Repeat 5`, medians, Linux, software GL): the quiet HUD does replay (counters over a 180-frame run: 9 pictures recorded at setup, 180 replays, 325 elements painted in total, all at setup), but its Paint time moved little (0.43 -> 0.40 ms) because replaying 1,995 painter calls into NanoVG on llvmpipe is most of that time; what pictures removed is the element walk and the cascade. Rows with more elements than calls gain most.

| Row | Paint ms before (step 6) | Paint ms after | Total ms before | Total ms after |
| --- | ---: | ---: | ---: | ---: |
| `paint-mix / quiet / solid` | 0.464 | 0.265 | 0.646 | 0.450 |
| `paint-mix / quiet / border` | 0.565 | 0.375 | 0.765 | 0.572 |
| `paint-mix / quiet / radial-gradient` | 0.509 | 0.270 | 0.695 | 0.467 |
| `inspector / scroll` | 0.288 | 0.202 | 0.547 | 0.413 |
| `table / quiet` | 0.293 | 0.234 | 0.380 | 0.319 |
| `hud / quiet` | 0.422 | 0.402 | 0.520 | 0.500 |

(`-Repeat 1` run for those before/after pairs; the `-Repeat 5` matrix against `baseline-linux` is in the table of each earlier step and shows no row slower than the baseline except `hud / hover / layout` +10% and `table / hover / layout` +5%, which now pay for the layout the owner asked hover to do.)

Pixels: screenshots of the table, inspector and HUD scenes under xvfb before (`dev` at step 6) and after, taken after the same warmup: table and inspector are pixel-identical (max difference 0) in quiet, scroll and hover rows; the HUD differs only inside the 114 x 22 px box of its pulsing alert (max 18 of 255), and two runs of the old build differ there by up to 29 of 255 because the pulse follows the wall clock. Unverified: other platforms' renderers.

### 5 — Renderer, NanoVG removed

The only GPU implementation of `Picture`. Playback may stay on the main thread. A second thread is legal only as a consumer of an immutable picture snapshot, and only after this step; the library still does not spawn threads (same split as a Noesis `Update` / `UpdateRenderTree`, client-driven).

Included, or NanoVG cannot be deleted:

- Rounded rects via SDF, color in the vertex. One draw for a run that shares a clip.
- Text quads from an atlas this renderer owns. `measure_text`, `break_lines`, and font metrics move here, including the layout path that step 6 still calls through the old painter.
- Images and nine-slice, one draw per atlas.
- Linear, radial, and conic gradients.
- Arcs.
- `fill_path` for formula outlines (`src/ui/math/math_paint.cpp`).
- Clips. An axis-aligned rect is a vertex attribute and a fragment discard, so it does not split a batch. A clip that is rotated with `apply_transform` is implemented in this renderer (stencil or a path), not left on `nvgScissor`.

`external/nanovg` and `src/render/opengl/nanovg_painter.cpp` leave in this step. GPU tests stay outside `engine_tests` ([Boundaries](Boundaries.md)). CPU coverage is the batch list: input commands → batches, asserted in `engine_tests`.

Done when the three scenes match step 4's pictures without linking NanoVG, and the HUD draw-call count is the batch-list count from the test.

### 7b — Hot and cold element

After pictures exist, a quiet frame no longer walks style caches. Hit-test and a dirty frame still do.

The hot record is the tree link, parent id, `layout_rect`, and the dirty flags. Style-cache payload, caret, IME, formula cache, and scrollbar chrome move to a side block addressed by the same id. One lookup per visit, not a hash map per field.

Done when a hit-test walk touches the hot record, and a test that paints a quiet picture does not read the side block.

### 8 — Explicit offscreen target

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
