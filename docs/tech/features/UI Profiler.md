---
tags: [feature]
---

# UI Profiler

A window the game turns on with `ui::set_ui_profiler_enabled(world, bool)` ([[include.engine.ui.profiler.h]]). The engine does not bind a key. Debug and RelWithDebInfo define `ENGINE_UI_PROFILER`. Release and MinSizeRel do not. In those configurations the toggle is an inline no-op, and `ENGINE_UI_PROFILE` ([[src.ui.profile.h]]) is `((void)0)`, so the call sites do not read the clock. The window, the rings, and the stage strings live in [[src.ui.profiler.cpp]] under the macro, so the linker drops them.

`kApiEpoch` stays 4. Sample storage is not on a public struct. `CmdDrawUI::canvas` is present in every configuration so the command layout does not change between Debug and Release. Copying it is not a clock.

## What is timed

CPU time only. There is no GPU timer query and no per-element flame graph.

While the macro is on, the window is closed, and CLI capture is off, a scope reads one bool and does not read the clock. While the window is open or capture is on, samples go into a ring of 120 frames. The ring for a canvas is allocated the first time that canvas is seen, then reused. The next `begin_frame` pushes the open frame, unless Pause is on, in which case the open frame is dropped and the ring stays. The panel reads the ring at Bind, so it shows the last finished frame. `wind-cli profile` sets capture without opening the window. `profile stop` clears that flag and does not close the window. Closing the window drops the rings only when capture is off, so a capture started from the CLI keeps the clock after the panel is gone. The first `profile` before a frame has been committed under capture waits for that commit. See [[features/CLI]].

Shared rows are not attributed to the selected canvas:

- `begin_frame`: fit and the inspector window sync. The profiler window sync runs after that scope.
- `run_ui_render`: building `CmdDrawUI`. A frame with no command buffer returns before the scope.

Per canvas:

- Bind: `apply_bindings`, then the stylesheet merge. A canvas with no view-model has no bind cost.
- Input: `prepare_top_canvas`, added to the canvas it returned. A miss adds nothing. Several events in one frame sum.
- Inside `paint_document`: layout (style plus layout), motion, and paint. A flag records whether the dirty-gate actually ran layout. After the paint timer, if the profiler is recording, the canvas's elements and generated items are counted. That walk is not part of the paint time.

A canvas tagged `InspectorPanel` or `ProfilerPanel` is skipped at every timing site.

## Panel

One window, titled "UI Profiler", 480×640 and resizable. Its canvas is `FillWindow`, `order` 10001, tagged `ProfilerPanel`. The windowed presentation installs `ProfilerWindowHost` beside `InspectorWindowHost` and opens it through `IWindowControl`. Headless `engine_tests` have no host: the panel still gets a canvas and a `WindowSizes` entry (`WindowId{0xFFFFFFF1}`, never passed to the window manager), and no OS window is opened. A real host does not pre-set `WindowSizes`; the presentation backfills the client size. `begin_frame` opens the window if it is not up yet. Turning the profiler off destroys the canvas and closes that window. It drops the rings when CLI capture is off. A close request for the profiler window does the same and does not close `kPrimaryWindow`. The close event is still delivered to the game; the profiler reads it with its own cursor.

The list is every canvas that is not the inspector panel and not the profiler panel, ordered by window, then `order`, then entity index. A row label is the root id, or `Canvas` when the id is empty. When two or more windows have a canvas, the label is prefixed with `[{id}] `. Clicking a row selects that canvas. If the selection dies, the next fill selects the first remaining canvas.

Under the list: Pause, then one line per stage for the selection (last, average, max, in milliseconds) and the two shared lines. Layout says when this frame skipped it. Element and generated counts sit on the same block.
