# UI profiler

The profiler has two halves. The engine keeps the probe: the `ENGINE_UI_PROFILE` scopes, the rings, and snapshot functions that return plain data. The editor shows the panel: the Profiler tab of `wind_editor` ([Editor](Editor.md)). A game cannot open it. To profile a game, Play it in the editor. `wind-cli profile` captures without the editor ([CLI](CLI.md)).

Header: `include/engine/ui/profiler.h`. Private: `src/ui/profile.h`. Panel: `editor/src/profiler_panel.h`.

`ENGINE_UI_PROFILER` is defined for Debug and RelWithDebInfo. Release and MinSizeRel do not define it. The public functions are then inline no-ops that return nothing, and `kUiProfilerBuilt` is false. `ENGINE_UI_PROFILE` is `((void)0)`, so those call sites do not read the clock. The rings and the stage strings in `src/ui/profiler.cpp` are under the macro.

`CmdDrawUI::canvas` is present in every configuration so the command layout does not change. Copying it is not a clock.

## What is timed

CPU time only. There is no GPU timer query and no per-element graph.

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

## Snapshot

| Function | Returns |
| --- | --- |
| `set_ui_profiler_attached(world, on)` | Starts or stops recording. Detaching clears the selection and Pause, and drops the rings unless capture is on |
| `profiler_canvases(world)` | `ProfilerCanvas` per canvas with a live tree, by window, then `order`, then entity index: entity, window, label, `selected`, frames stored |
| `profiler_select(world, canvas)`, `profiler_selected(world)` | The selected canvas. When it is gone, `profiler_canvases` selects the first |
| `set_profiler_paused(world, on)`, `profiler_paused(world)` | Pause |
| `profiler_frames(world, canvas)` | That canvas's ring, oldest first: `ProfilerFrame` (`stage_ns` per stage, `layout_ran`, `saw_paint`, `saw_bindings`, element and generated counts, `layout_skipped()`) |
| `profiler_shared_frames(world)` | The shared ring, oldest first |

A label is the root id, or `Canvas` when the id is empty. When two or more windows have a canvas, the label starts with `[{id}] `.

## Editor panel

The Profiler tab: a Pause checkbox and a hint, the canvas list on the left, the charts and the numbers on the right (`editor/assets/ui/profiler.xml`, `editor/assets/css/panels.css`). See [Editor](Editor.md) for when it attaches.

`ProfilerPanel::refresh` runs in the editor world's `Phase::Game` while the tab is visible. It copies the canvas list into `ProfilerRowViewModel`s (clicking a row calls `profiler_select`), copies the selected ring and the shared ring into the two chart paints, and writes the numbers. Pause is two-way like the inspector's Pick. In Release the panel shows only a hint that the profiler is not in this build.

| Chart | Size | Contents |
| --- | --- | --- |
| Selected canvas | 160px tall | one column per ring slot, stored frames on the right. Stages stack from the bottom: bindings, stylesheets, input, layout, motion, paint |
| Shared | 40px | `begin frame` and `commands`, on their own scale |

Both charts are `ProfilerChartPaint`, an `IPaint` on the view-model drawn through `IDrawList`. The geometry is `editor/src/profiler_chart.h`. Y is milliseconds. The ceiling is the smallest of 1, 2, 4, 8, … ms that covers the tallest column, and is never below 1 ms; the title above the chart shows it. When that ceiling is above 16.7 ms, a horizontal line marks the 60 fps budget. A frame that painted and skipped layout draws a 2px tick at the bottom of its column. An empty ring draws no columns. Paint draws the columns copied during refresh; it does not read the game world.

The numbers are last, average, and max of each stage over the ring, `skipped` after layout when the last frame skipped it, the element and generated counts of the last frame, then the two shared stages.

The panel profiles the world bound to `kPrimaryWindow`. A second world of the game is not listed.

## Tests

`tests/ui_profiler_test.cpp`: nothing stored while detached, the shared ring, bind samples per canvas, another world's passes stay out, layout then a skip, Pause, the ring size, the canvas list and selection, the window prefix, capture keeps the rings. `profile` over the socket is `tests/cli_server_test.cpp`. `editor/tests/profiler_panel_test.cpp` and `editor/tests/profiler_chart_test.cpp`: the panel with and without frames, Pause, detach, chart geometry, and the chart paint.

## See also

- [Editor](Editor.md)
- [UI](../modules/UI.md)
- [Boundaries](../architecture/Boundaries.md)
- [UI Performance Plan](../architecture/UI%20Performance%20Plan.md)
