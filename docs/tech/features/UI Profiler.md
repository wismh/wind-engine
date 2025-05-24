# UI profiler

The game turns it on with `ui::set_ui_profiler_enabled(world, enabled)`. The engine does not bind a key.

`ENGINE_UI_PROFILER` is defined for Debug and RelWithDebInfo. Release and MinSizeRel do not define it. The toggle is then an inline no-op. `ENGINE_UI_PROFILE` in `src/ui/profile.h` is `((void)0)`, so those call sites do not read the clock. The window, the rings, and the stage strings in `src/ui/profiler.cpp` are under the macro.

`CmdDrawUI::canvas` is present in every configuration so the command layout does not change. Copying it is not a clock. Sample storage is not a public struct.

## What is timed

CPU time only. There is no GPU timer query and no per-element graph.

While the macro is on, the window is closed, and CLI capture is off, a scope reads one bool and does not read the clock. While the window is open or capture is on, samples go into a ring of 120 frames. The ring for a canvas is allocated the first time that canvas is seen, then reused.

Each `begin_frame` starts with `profiler_commit_frame`. That pushes every touched open slot, unless Pause is on, in which case the open frame is dropped and the ring stays. When the frame schedule runs, `begin_frame` is called twice: from `simulate_worlds` before the schedules, and at the start of `run_input`. Bind reads the rings after that second commit. The shared ring's last slot is only the pre-schedule fit and inspector sync. `commands` from the previous tick were committed one shared slot earlier, in the same sample as the fit and inspector sync from that tick's `run_input`. The second commit does not push a canvas slot, so the canvas slot the panel treats as last is the previous tick's input, bindings, stylesheets, layout, motion, and paint. Nothing commits again before the charts paint, so they read those same slots.

`wind-cli profile` sets capture without opening the window. `profile stop` clears that flag and does not close the window. Closing the window drops the rings only when capture is off. The first `profile` before a frame has been committed under capture waits for that commit. See [CLI](CLI.md).

Shared rows, not attributed to the selected canvas:

| Row | What it covers |
| --- | --- |
| `begin_frame` | fit and the inspector window sync. Profiler window sync runs after that scope |
| `commands` | building `CmdDrawUI` inside `run_ui_render`. A frame with no command buffer returns before the scope |

Per canvas:

| Stage | What it covers |
| --- | --- |
| Bindings | `apply_bindings` |
| Stylesheets | stylesheet merge after bind. A canvas with no view-model has no bind cost |
| Input | `prepare_top_canvas`, added to the canvas it returned. A miss adds nothing. Several events in one frame sum |
| Layout | style plus layout inside `paint_document`. A flag records whether the dirty gate actually ran layout |
| Motion | style animation sample |
| Paint | the rest of `paint_document` |

After the paint timer, if recording, the canvas's elements and generated items are counted. That walk is not part of the paint time.

A canvas tagged `InspectorPanel` or `ProfilerPanel` is skipped at every timing site.

## Panel

One window, title `"UI Profiler"`, 480×760, resizable. Its canvas is `FillWindow`, `order` 10001, tagged `ProfilerPanel`.

The windowed presentation installs `ctx<ProfilerWindowHost>()` beside the inspector host. Headless tests have an empty host: the panel still gets a canvas and a `Presentation.sizes` entry at `WindowId{0xFFFFFFF1}`, and that id is never passed to the window manager. A real host does not pre-set `Presentation.sizes` for that id. `poll` backfills a missing secondary id from `drawable_size()` into `Presentation.sizes`.

`begin_frame` opens the window if it is not up yet. Turning the profiler off destroys the canvas and closes that window, and drops the rings when CLI capture is off. A close request for the profiler window does the same and does not close `kPrimaryWindow`. The close event is still delivered. The profiler reads it with its own cursor.

The list is every canvas that is not a tool panel, ordered by window, then `order`, then entity index. A row label is the root id, or `Canvas` when the id is empty. When two or more windows have a canvas, the label is prefixed with `[{id}] `. Clicking a row selects that canvas. If the selection dies, the next fill selects the first remaining canvas.

Under the list: Pause, then the frame charts, then the numbers.

| Chart | Size | Contents |
| --- | --- | --- |
| Selected canvas | 120px tall | one column per ring slot, stored frames on the right. Stages stack from the bottom: bindings, stylesheets, input, layout, motion, paint |
| Shared | 32px | `begin frame` and `commands`, on their own scale |

Y is milliseconds. The ceiling is the smallest of 1, 2, 4, 8, … ms that covers the tallest column, and is never below 1 ms. When that ceiling is above 16.7 ms, a horizontal line marks the 60 fps budget. A frame that painted and skipped layout draws a 2px tick at the bottom of its column. Pause freezes the rings. An empty ring draws no columns.

Both charts are `IPaint` on the panel view-model and read the rings during paint. The samples are not view-model properties. The numeric lines are last, average, max, layout skipped, and element and generated counts.

## Tests

`tests/ui_profiler_test.cpp`. The scopes can be asserted without a window. `profile` over the socket is `tests/cli_server_test.cpp`.

## See also

- [UI](../modules/UI.md)
- [Boundaries](../architecture/Boundaries.md)
- [UI Performance Plan](../architecture/UI Performance Plan.md)
