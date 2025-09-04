# UI bench

`wind_ui_bench` renders one UI scene in one mode, records the [UI Profiler](UI%20Profiler.md) for a fixed number of frames, writes the profiler snapshot with the run's metadata to a JSON file, and exits. Its three reference scenes are the baseline of the [UI Performance Plan](../architecture/UI%20Performance%20Plan.md#baseline). It lives in the engine repo (`bench/`) and is built against the static engine like [Launcher](Launcher.md).

## Build and run

```bash
cmake --preset vs-bench
cmake --build build-bench --config RelWithDebInfo --target wind_ui_bench
build-bench/bin/RelWithDebInfo/wind_ui_bench --scene table --mode quiet
```

Reference numbers come from RelWithDebInfo. The profiler (`ENGINE_UI_PROFILER`) is compiled in Debug and RelWithDebInfo only; a Release build prints that the profiler is not built and exits with 3. See [CMake](../build/CMake.md#bench-build).

## Command line

```
wind_ui_bench --scene <name> --mode <name> [--variant <name>] [--warmup N] [--frames N] [--out path.json]
              [--width N --height N]
wind_ui_bench --list
```

| Flag | Default | |
| --- | --- | --- |
| `--warmup` | 60 | ticks before the rings are cleared; at least 5 |
| `--frames` | `kProfilerRingFrames` (120) | measured frames, 1 to 120, so the ring is exactly the measured window |
| `--out` | `ui_bench-<scene>-<mode>[-<variant>].json` | in the working directory |
| `--width`, `--height` | 1600, 900 | window size |
| `--list` | | every row of the matrix, one per line: `scene mode [variant]` |

A bad argument or a combination outside the matrix prints what is wrong and what is valid there, and exits with 2. A run that does not record its frames exits with 1.

## Matrix

One table in code, `bench_matrix()` (`bench/src/bench_matrix.cpp`), drives `--list` and the validation.

| Scene | Modes |
| --- | --- |
| `table` | `quiet`, `one-change`, `hover` (`paint`, `layout`), `scroll`, `churn` |
| `inspector` | `quiet`, `one-change`, `hover` (`paint`, `layout`), `scroll`, `churn` |
| `hud` | `quiet`, `one-change`, `hover` (`paint`, `layout`) |
| `paint-mix` | `quiet` with `solid`, `rounded`, `border`, `linear-gradient`, `radial-gradient`, `conic-gradient`, `image`, `nine-slice`, `text`, `arc`, `math` |
| `motion` | `quiet` with `paint-props`, `layout-props`, `both` |
| `text` | `quiet`, `scroll` |
| `clip` | `quiet`, `scroll` |

## Scenes

XML and CSS under `bench/assets/`; view-models in C++; data from `kBenchSeed` through `BenchRandom` (SplitMix64, the same sequence on every compiler).

| Scene | What it is |
| --- | --- |
| `table` | 10,000 rows of five columns (id, name, value, status, ratio), a header, a zebra stripe through `var-stripe`, in a `ScrollView`. Rows are 24 px, so the `ItemsControl` virtualizes them. Rows carry a command, so they are hit and hovered |
| `inspector` | 22 collapsible sections of 4 groups: text rows (`TextInput`), numeric rows (stepper `Button`s around a `TextInput`), flag rows (`Checkbox`), Reset and Apply. About 2,900 elements, none virtualized |
| `hud` | A resource bar of 8 bound numbers and an alert that pulses forever (`@keyframes`), a `Viewport` whose world is an `IPaint` (grid, 400 units, 12 selection rings), a 36-row unit list, a selection panel, six order buttons, an `IPaint` minimap. The camera never moves |
| `paint-mix` | 1,000 cells of 34 x 28 px (25 rows of 40), every cell one painter call kind. Box kinds share `paint_mix.xml` and differ by a `mix_*.css` sheet; `text`, `math`, and `arc` (an `IPaint`) have their own document. `image` and `nine-slice` use two 32 px PNGs made for the bench |
| `motion` | 200 boxes and 20 bars. The variant sheet animates the boxes' opacity and transform, the bars' width, or both |
| `text` | 60 wrapped paragraphs at 22, 16, and 12 px, then 200 identical rows, in one `ScrollView`, nothing virtualized |
| `clip` | An outer `ScrollView` with a column of notes and twelve inner `ScrollView`s, then a block rotated 6deg whose `ScrollView` holds one more |

Every document has an element with id `rest`, where the pointer waits, and the scroll scenes one with id `wheel`, where the wheel turns. Elements with class `hot` are the hover targets. The reference scenes merge `hover_paint.css` (`.hot:hover` changes the background) after their own sheet, or `hover_layout.css` (`.hot:hover` changes the padding) in `hover layout`. Layout resolves style without pseudo-classes today, so the layout rule restyles but does not lay out.

## Modes

| Mode | Every driven tick |
| --- | --- |
| `quiet` | Nothing. The pointer was moved once, to `rest`, before the warmup ended |
| `one-change` | One bound value takes a new value: the frame counter of `table` and `inspector`, the gold of `hud` |
| `hover` | One pointer move onto the next `hot` element that is on screen and is what a hit test there returns |
| `scroll` | One wheel event of -0.1 units (4 px) at `wheel` |
| `churn` | `table` removes a row and inserts a new one within the top 30; `inspector` collapses a section on even steps and expands it on the next |

## Measurement

`BenchApp` (`bench/src/bench_app.cpp`) spawns one `FillWindow` canvas on its world, calls `set_ui_profiler_attached`, and turns vsync off (`IWindowControl::set_vsync(false)`). It owns the pointer: it replaces the `InputSystem` router with one that passes events only while the bench itself calls `handle_mouse_move` or `handle_mouse_wheel`, so the real mouse and keyboard change nothing.

Its frame system (`Phase::Game`) follows `BenchSchedule`: on tick 3 it binds the document once more and fails on a missing binding (the engine's bind pass drops that error), looks up `rest`, `wheel`, and the hover targets in the laid-out tree, and rests the pointer; ticks after that drive the mode. On tick `warmup` it calls `profiler_clear`, so the ring keeps that tick and the ones after. When the canvas's ring holds `frames` frames it writes the report and quits. A run that does not get there within 600 more ticks fails.

## Report

```json
{"bench":{"scene":"table","mode":"quiet","variant":"","warmup":60,"frames":120,
  "window":{"width":1600,"height":900,"drawable_width":1600,"drawable_height":900},
  "vsync":false,"hover_targets":0,"config":"RelWithDebInfo","engine_version":"0.1.0","build_id":"...",
  "commit":"<git HEAD at configure time, empty without git>","dirty":true,"timestamp":"2026-10-09T19:10:36Z"},
 "profile":{ ...profiler_json: the wind-cli profile result... }}
```

`profile` is `profiler_json` ([UI Profiler](UI%20Profiler.md#snapshot)): per canvas the stage times, `draw_calls`, and `paint_commands`, each last, average, and max over the measured frames, plus the element counts of the last frame.

## Matrix runs

`bench/run_matrix.ps1` (Windows PowerShell 5.1) runs every row of `--list` as its own process, one after another, and stops with exit 1 on the first run that fails.

```powershell
powershell -ExecutionPolicy Bypass -File bench/run_matrix.ps1 -Repeat 3
powershell -ExecutionPolicy Bypass -File bench/run_matrix.ps1 -Filter 'table/*,hud/quiet' -Repeat 3
powershell -ExecutionPolicy Bypass -File bench/compare.ps1 -Base bench/results/baseline -Head bench/results/<dir>
```

| Parameter | Default | |
| --- | --- | --- |
| `-Exe` | `build-bench/bin/RelWithDebInfo/wind_ui_bench.exe` | |
| `-OutDir` | `bench/results/<yyyy-MM-dd>-<short commit>[-dirty]` | commit and dirty from the first report; `-2`, `-3` when it exists |
| `-Filter` | every row | wildcards over `scene/mode` or `scene/mode/variant`, comma-separated or an array |
| `-Warmup`, `-Frames` | the bench's | passed through |
| `-Repeat` | 1 | passes over the filtered rows; each row's numbers are the median across passes |

It writes `runs/<row>.r<pass>.json` (the raw reports), `summary.json`, and `summary.md`. The header records commit, dirty, config, engine version and build id, window, warmup, frames, repeat, date, and the machine (CPU and GPU from CIM, OS, power plan). Per row:

| Column | |
| --- | --- |
| `elements`, `generated` | of the last frame |
| stage ms | `avg_ms` of each stage |
| `commands` | 2 x the shared `commands` average: the shared ring holds two slots per tick ([UI Profiler](UI%20Profiler.md#what-is-timed)) |
| `total` | the six stages + `commands`, the CPU ms of a frame |
| `spread` | (max - min) / median of `total` across passes |
| `peak` | sum of each stage's `max_ms`: an upper bound of the worst frame, the stage maxima may come from different frames |
| `layout skip` | the last frame skipped layout. The report has no count of frames that ran layout, so there is no ran fraction; `layout_max_ms` is in the JSON |
| `draws` | `draw_calls` average |
| `state` | `save`, `restore`, `scissor`, `transform`, `view`, `opacity`, `font` calls |
| `top` | the three most frequent drawing calls (every kind is in `paint_commands` of the JSON) |

A scene with several canvases is summed and its row says how many; every scene today has one.

`bench/compare.ps1 -Base <dir|summary.json> -Head <dir|summary.json> [-Threshold 5]` prints a markdown table of the rows both have: total, layout, paint ms, and draw calls, base -> head with the change. A row is `REGRESSION` when total grows more than the threshold and more than 0.01 ms, or draw calls grow more than the threshold. It warns when the machine, config, or window differs. It exits 0, or 1 when an input is missing.

`bench/results/baseline/summary.md` and `summary.json` are the step-0 baseline of the [UI Performance Plan](../architecture/UI%20Performance%20Plan.md#baseline) and are kept in git. Everything else under `bench/results/` (other runs, `baseline/runs/`) is ignored (`bench/results/.gitignore`). The baseline was taken on an uncommitted tree: its commit is the parent, `dirty` true.

### Repeatability

Measured on the baseline machine (Ryzen 5 5600H laptop, Balanced power plan), the six rows `table`, `inspector`, `hud` x `quiet`, `one-change`, four sets of `-Repeat 3`:

- Within one set, `spread` of `total` is 1 to 20%, mostly 3 to 12%. In the full baseline run the same six rows spread 3 to 29% (`table/one-change`), and the whole matrix 1 to 41% (`clip/quiet`, 1.2 ms). Single passes differ by random spikes, not by order: the first pass is not consistently slower.
- The medians of the four sets agree within 2% (`inspector/quiet`, 3.2 ms), 5% (`table/quiet`, 0.46 ms), and 15% (`hud/one-change`, 0.8 ms). Sub-millisecond rows are the noisy ones.
- `-Warmup 300` and High process priority did not narrow it. The power plan was left as it was.

So compare medians of `-Repeat 3` or more, read a sub-millisecond row's change under about 15% as noise, and treat the default `-Threshold 5` as a flag to look at, not a verdict. Draw calls and painter-call counts are exact and repeat run to run.

## Tests

`bench/tests/bench_test.cpp` (`wind_ui_bench_tests`): option parsing and every refusal, resolving a case with the valid choices in the message, the default report name, the matrix (unique rows, every row resolves by its names, the reference scenes' modes), the schedule, the frame plan of each mode (hover never repeats a target), seeded data (random sequence, table records, paragraphs, units), the report JSON and its escaping, the UTC timestamp, the rest and hover points on a hand-laid tree, and a parse of every bench stylesheet without a warning and every bench document with a `rest` element. The runs themselves need a window and are not in ctest.

## See also

- [UI Profiler](UI%20Profiler.md)
- [UI Performance Plan](../architecture/UI%20Performance%20Plan.md)
- [CMake](../build/CMake.md#bench-build)
