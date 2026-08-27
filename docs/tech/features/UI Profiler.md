# UI profiler

The profiler has two halves. The engine keeps the probe: the `ENGINE_UI_PROFILE` scopes, the rings, and snapshot functions that return plain data. The editor shows the panel: the Profiler tab of `wind_editor` ([Editor](Editor.md)). A game cannot open it. To profile a game, Play it in the editor. `wind-cli profile` captures without the editor ([CLI](CLI.md)).

Header: `include/engine/ui/profiler.h`. Private: `src/ui/profile.h`. Panel: `editor/src/profiler_panel.h`.

`ENGINE_UI_PROFILER` is defined for Debug, RelWithDebInfo, and every configuration of the editor build (`ENGINE_EDITOR`), so the Release editor from the SDK profiles. An exported game's Release and MinSizeRel do not define it. The public functions are then inline no-ops that return nothing, and `kUiProfilerBuilt` is false. `ENGINE_UI_PROFILE` is `((void)0)`, so those call sites do not read the clock. The rings and the stage strings in `src/ui/profiler.cpp` are under the macro.

`CmdDrawUI::canvas` is present in every configuration so the command layout does not change. Copying it is not a clock.

## What is timed

CPU time, plus the counters below. There is no GPU timer query and no per-element graph.

One world records at a time: the last one attached with `set_ui_profiler_attached` or captured by `wind-cli profile`. Every engine pass over a world (`begin_frame`, input, bind, command build) starts with `profiler_attach(world)`. Scopes record only while that pass is over the recording world. Paint has no world: `run_ui_render` sets `CmdDrawUI::canvas` only when its world is the recording one, and `paint_document` records exactly when that field is set. So the editor's own canvases, in the editor's world, never reach the game's rings, even when an entity has the same index in both worlds.

While the macro is on and the current pass is not over the recording world, a scope compares two pointers and does not read the clock. While recording, samples go into a ring of `kProfilerRingFrames` (120) frames. The ring for a canvas is allocated the first time that canvas is seen, then reused.

Each `begin_frame` starts with `profiler_commit_frame`. That pushes every touched open slot, unless Pause is on, in which case the open frame is dropped and the rings stay. When the frame schedule runs, `begin_frame` is called twice: from `simulate_worlds` before the schedules, and at the start of `run_input`. The second commit does not push a canvas slot, so a canvas slot holds one tick: input, bindings, stylesheets, layout, motion, and paint. The shared ring alternates: the pre-schedule fit, then the `run_input` fit with that tick's `commands`.

The editor's world is added before the game's. `simulate_worlds` calls `begin_frame` on every world before any schedule, so the editor's `Phase::Game` reads the rings right after the game's first commit of the tick. The last canvas slot is the game's previous tick, complete. The last shared slot is that tick's `run_input` fit and `commands`.

`wind-cli profile` sets capture without the editor. `profile stop` clears that flag and does not detach a panel. Detaching drops the rings only when capture is off. The first `profile` before a frame has been committed under capture waits for that commit. See [CLI](CLI.md).

Shared rows, not attributed to a canvas:

| Row | What it covers |
| --- | --- |
| `begin_frame` | canvas fit |
| `commands` | building `CmdDrawUI` inside `run_ui_render`. A frame with no command buffer returns before the scope |

Per canvas (`ProfilerStage`, the order a chart stacks them from the bottom):

| Stage | What it covers |
| --- | --- |
| Bindings | `apply_bindings` |
| Stylesheets | stylesheet merge after bind. A canvas with no view-model has no bind cost |
| Input | `prepare_top_canvas`, added to the canvas it returned. A miss adds nothing. Several events in one frame sum |
| Layout | style plus layout inside `paint_document`. A flag records whether the dirty gate actually ran layout |
| Motion | style animation sample |
| Paint | the rest of `paint_document` |

After the paint timer, the canvas's elements and generated items are counted. That walk is not part of the paint time.

## Counters

Per canvas, on `ProfilerFrame`, recorded under the same rule as the paint scope (`CmdDrawUI::canvas` set):

| Field | What it counts |
| --- | --- |
| `paint_commands[ProfilerPaintKind]` | calls into `IUiPainter` during the paint pass: the element tree, `IPaint` through `IDrawList`, and the inspector boxes. Base pass and popup layer add up |
| `draw_calls` | GPU draw calls the painter queued during those calls |

`ProfilerPaintKind` has one bucket per drawing or state call of `IUiPainter` (`src/ui/painter.h`): `save`, `restore`, `scissor`, `transform`, `view`, `opacity`, `fill_rect` / `fill_rounded_rect` (`fill_rounded_rect` with a zero or a positive radius), `linear_gradient` / `radial_gradient` / `conic_gradient` (`fill_rounded_rect_gradient` by `GradientKind`), `stroke_rect`, `line`, `arc`, `path`, `font`, `text`, `image`, `image_repeat`, `nine_slice`. `profiler_paint_kind_name` gives the names. Text measuring is not counted.

`paint_document` paints through `ProfilerPaintCounter` (`src/ui/profiler_paint_counter.h`), a decorator that counts and forwards every call. When the canvas is not recorded it hands out the wrapped painter itself. Layout keeps the wrapped painter, because the layout dirty gate compares painter identity. The counter adds its counts with `profiler_add_paint` after the paint timer stops. While a canvas is recorded, its Paint time includes the counter's forwarding: one extra virtual call per painter call.

Draw calls are per canvas, not per flush. One painter per window queues every canvas of that window, and NanoVG flushes the queue once, at `nvgEndFrame` after the last canvas. `IUiPainter::queued_draw_calls` returns the draw calls queued so far this frame; the counter reads it before and after the canvas and records the difference. So the canvases of one world on one window sum to that world's share of the flush, and another world's canvases on the same window are not in it. A painter that does not override it reports 0.

`NanoVgPainter::queued_draw_calls` walks the NanoVG GL backend's queued calls (`GLNVGcontext::calls`, through `nvgInternalParams(vg)->userPtr`; the implementation is compiled into `nanovg_painter.cpp`) and adds the `glDrawArrays` that `glnvg__renderFlush` will issue for each. It keeps a cursor, so each call is read once per frame. `begin_frame` resets it.

| Call type | `glDrawArrays` |
| --- | --- |
| `GLNVG_FILL` | per path a stencil fan, plus a fringe strip with `NVG_ANTIALIAS`; then one cover quad |
| `GLNVG_CONVEXFILL` | per path a fan, plus a fringe strip when the path has one |
| `GLNVG_STROKE` | per path 3 strips with `NVG_STENCIL_STROKES` (the engine creates the context with it), else 1 |
| `GLNVG_TRIANGLES` | 1 (a text run, `nvgText`) |

That table follows `glnvg__fill`, `glnvg__convexFill`, `glnvg__stroke`, and `glnvg__triangles`; a NanoVG update must check it. Uniform and buffer uploads are not counted.

## Snapshot

| Function | Returns |
| --- | --- |
| `set_ui_profiler_attached(world, on)` | Starts or stops recording. Detaching clears the selection and Pause, and drops the rings unless capture is on |
| `profiler_canvases(world)` | `ProfilerCanvas` per canvas with a live tree, by window, then `order`, then entity index: entity, window, label, `selected`, frames stored |
| `profiler_select(world, canvas)`, `profiler_selected(world)` | The selected canvas. When it is gone, `profiler_canvases` selects the first |
| `set_profiler_paused(world, on)`, `profiler_paused(world)` | Pause |
| `profiler_frames(world, canvas)` | That canvas's ring, oldest first: `ProfilerFrame` (`stage_ns` per stage, `layout_ran`, `saw_paint`, `saw_bindings`, element and generated counts, `paint_commands`, `draw_calls`, `layout_skipped()`) |
| `profiler_shared_frames(world)` | The shared ring, oldest first |
| `profiler_clear(world)` | Empties every ring. The open frame, selection, Pause, and recording stay, so the next commit is a whole tick. `wind_ui_bench` clears after its warmup ([UI Bench](UI%20Bench.md)) |
| `profiler_json(world)` | The `wind-cli profile` result object: `paused`, `capturing`, per canvas `window`, `id`, `frames`, `elements`, `generated`, `layout_skipped`, `stages` (`last_ms`, `avg_ms`, `max_ms` each), `draw_calls` and every `paint_commands` kind (`last`, `avg`, `max`), and `shared`. Empty without `ENGINE_UI_PROFILER` |

A label is the root id, or `Canvas` when the id is empty. When two or more windows have a canvas, the label starts with `[{id}] `.

## Editor panel

The Profiler tab: a Pause checkbox and a hint, the canvas list on the left, the charts and the numbers on the right (`editor/assets/ui/profiler.xml`, `editor/assets/css/panels.css`). See [Editor](Editor.md) for when it attaches.

`ProfilerPanel::refresh` runs in the editor world's `Phase::Game` while the tab is visible. It copies the canvas list into `ProfilerRowViewModel`s (clicking a row calls `profiler_select`), copies the selected ring and the shared ring into the two chart paints, and writes the numbers. Pause is two-way like the inspector's Pick. The editor always has the profiler: `profiler_panel.cpp` has a `static_assert` on `kUiProfilerBuilt`, so an editor without `ENGINE_UI_PROFILER` does not compile.

| Chart | Size | Contents |
| --- | --- | --- |
| Selected canvas | 160px tall | one column per ring slot, stored frames on the right. Stages stack from the bottom: bindings, stylesheets, input, layout, motion, paint |
| Shared | 40px | `begin frame` and `commands`, on their own scale |

Both charts are `ProfilerChartPaint`, an `IPaint` on the view-model drawn through `IDrawList`. The geometry is `editor/src/profiler_chart.h`. Y is milliseconds. The ceiling is the smallest of 1, 2, 4, 8, … ms that covers the tallest column, and is never below 1 ms; the title above the chart shows it. When that ceiling is above 16.7 ms, a horizontal line marks the 60 fps budget. A frame that painted and skipped layout draws a 2px tick at the bottom of its column. An empty ring draws no columns. Paint draws the columns copied during refresh; it does not read the game world.

The numbers (`editor/src/profiler_stats.h`) are last, average, and max of each stage over the ring, `skipped` after layout when the last frame skipped it, the element and generated counts of the last frame, last, average, and max draw calls, the painter calls of the last frame that are not zero (`paint  fill_rect 12  text 3`), then the two shared stages.

The panel profiles the world bound to `kPrimaryWindow`. A second world of the game is not listed.

## Tests

`tests/ui_profiler_test.cpp`: nothing stored while detached, the shared ring, bind samples per canvas, another world's passes stay out, layout then a skip, Pause, the ring size, the canvas list and selection, the window prefix, capture keeps the rings, painter calls by kind against a recording painter, no counts while not recorded, draw calls as each canvas's share of one painter's queue, counts per frame through the ring, and the JSON counters. `profile` over the socket is `tests/cli_server_test.cpp`. `tests/ui_profiler_test.cpp` also has `CompiledOutApiIsANoOp` for a build without the macro. `editor/tests/profiler_panel_test.cpp` and `editor/tests/profiler_chart_test.cpp`: the panel with and without frames, Pause, detach, the draw-call and painter-call lines, chart geometry, and the chart paint.

## See also

- [Editor](Editor.md)
- [UI](../modules/UI.md)
- [Boundaries](../architecture/Boundaries.md)
- [UI Performance Plan](../architecture/UI%20Performance%20Plan.md)
