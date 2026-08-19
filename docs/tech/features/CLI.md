# CLI

`wind-cli` is a host tool. It does not link `engine`. When `ENGINE_CLI_SERVER` is on, the game or the editor listens on `127.0.0.1` and answers `POST /exec`. The tool prints the JSON body and exits 0 when that body starts with `{"ok":true`. A body that does not start that way exits 1, as do a missing game and a failed exchange. `status` always exits 0. `launch` starts an editor ([Editor commands](#editor-commands)).

A command error is still HTTP 200 with `"ok":false`. Usage errors exit 2.

Debug, RelWithDebInfo, and every configuration of the editor build (`ENGINE_EDITOR`, so the Release editor too) define `ENGINE_CLI_SERVER`, and not on Emscripten or Android. An exported game's Release and MinSizeRel do not. Without the macro the game does not open a port and does not write a descriptor. `wind-cli` still builds. The only public header is `<engine/core/cli_commands.h>`, for a host's own commands ([Host commands](#host-commands)). The SDK installs `wind-cli` into `bin/`, beside `wind_editor`.

| File | Role |
| --- | --- |
| `include/engine/core/cli_commands.h` | `CliCommand`, `CliReply`, `CliCommands`: the host's commands, passed in `RunHooks::cli` |
| `src/cli/cli_server.h` | request types, `CliFrame`, `start` / `stop` / `begin_frame` / `drain` |
| `src/cli/cli_server.cpp` | socket, descriptor, accept thread, routing. Empty translation unit without the macro |
| `src/cli/cli_commands.cpp` | `tree`, `element`, `hit`, `click`, `profile`, `element_window_rect`, `canvas_window_rect`, canvas picking, and the JSON of a host reply |
| `src/cli/dock_commands.cpp` | `dock`: list, `activate`, `move`, `float`, `mode` ([Dock](#dock)) |
| `src/cli/screenshot.cpp` | `screenshot`: crop, PNG, reply ([Screenshot](#screenshot)) |
| `src/cli/json.h` | `Json`, the streaming writer both use |
| `editor/src/editor_cli.cpp` | the editor's `state`, `play`, `stop`, `open` ([Editor](Editor.md#wind-cli)) |
| `tools/wind_cli/main.cpp` | the host client and `launch` |

The tool does not check the engine build id.

## When it runs

`GameLoop::begin` calls `cli::start(RunHooks::cli.kind)`, after `on_start`. `GameLoop::end` calls `cli::stop`. `begin_frame` does not bind a port. A test that calls `begin_frame` does not either.

`tick` and `reentrant_tick` pass a `CliFrame`: `world_for` (`Worlds::world_for`, looked up per request) and `host` (`&RunHooks::cli`). Both calls run every frame, with or without a world on `kPrimaryWindow`.

1. `cli::begin_frame` runs an armed `click` before `flush_events` and simulate.
2. Before `draw_all`, `cli::capture_requests` names the windows an armed `screenshot` waits for. `draw_all` reads each of them back ([Screenshot](#screenshot)).
3. After `draw_all`, `cli::drain` answers `tree`, `element`, `hit`, `profile`, and `dock` from this frame's painted tree, answers an armed `screenshot` from `CliFrame::captures`, arms a `click` for the next `begin_frame` and a new `screenshot` for the next `draw_all`, and passes every other command to the host.

A UI command (`tree`, `element`, `hit`, `click`, `profile`, `dock`) runs against the world bound to the request's `window` (default 0, `kPrimaryWindow`). When that window has no world, the next `drain` answers `{"ok":false,"error":"no world on window N"}` at once; there is no 504. In the editor between plays `kPrimaryWindow` has no world; `--window` with the editor window's id reaches the editor's own canvases.

`execute()` inside the command runs a click immediately. The deferral is only the socket path, so the response is the drain after the click has been painted. `tests/cli_server_test.cpp` pumps the queue on the test thread. It does not go through `GameLoop`.

## Wire

The OS picks the port (`bind` with port 0). A descriptor is written for the current user and removed on stop.

| Platform | Directory |
| --- | --- |
| Windows | `%LOCALAPPDATA%\wind\cli\<pid>.json` |
| `XDG_RUNTIME_DIR` set | `$XDG_RUNTIME_DIR/wind/cli` |
| otherwise | `/tmp/wind-cli-<uid>` |

Fields: `pid`, `port`, `token`, `exe`, `kind`. `kind` is `RunHooks::cli.kind`, `game` when empty; the editor writes `editor`. `wind-cli` reads a descriptor without `kind` as a game. The token is 32 random bytes, base64. Every request sends `Authorization: Bearer`.

| Response | When |
| --- | --- |
| 400 | `Content-Length` is missing, is not a complete integer, or is greater than 1 MiB. Body is `{"ok":false,"error":"invalid request"}`. This check runs before the route check |
| 403 | the request has an `Origin` header, before the token is checked |
| 401 | missing or wrong token |
| 404 | anything other than `POST /exec` |
| 504 | the main thread did not answer within five seconds |
| 503 | the server is stopping and a waiter is still blocked |
| 200 | the JSON body, including `"ok":false` |

The accept thread queues the body and waits. Engine calls run on the main thread.

On Windows the descriptor file is created with an owner-only ACL (`D:P(A;;FA;;;OW)`).

## Commands

`status` lists live descriptors (`pid`, `port`, `kind`, `exe`) and does not contact the process. It does not print the token. A dead pid's file is removed. One live process is the target of a UI command. Several require `--pid`. The editor commands consider only `kind` `editor`, so a standalone game beside the editor does not need `--pid`. `--window` selects a `WindowId` (default 0, `kPrimaryWindow`). `--canvas` selects one canvas on it ([Canvases](#canvases)).

The JSON body is `{"command":"…"}` plus optional `selector`, `window`, `canvas`, `x`, `y`, and `path` (absolute, UTF-8). `profile stop` (and `--stop`) also sends `"stop":true`. `dock` adds `action`, `panel`, `node`, `zone`, `mode`, `space`, and `w`, `h` ([Dock](#dock)).

| Command | Result |
| --- | --- |
| `tree` | `result.nodes[]` for every canvas on that window, or for the one `--canvas` names |
| `element <selector>` | One element object (fields below) |
| `hit <x> <y>` | That same object, or `"result":null` |
| `click <selector>` | `executed`, and `reason` when it did not run |
| `screenshot [selector]` | `path`, `window`, `width`, `height`, `rect`. Writes a PNG ([Screenshot](#screenshot)) |
| `profile` | `result` timings below. `paused` is the editor panel's Pause |
| `profile stop` | `result.capturing` is false. Clears CLI capture only. Does not detach the editor's profiler panel |
| `dock [activate\|move\|float\|mode …]` | The world's dock spaces, or one layout change ([Dock](#dock)) |

`tree` nodes: `window`, `canvas`, `canvas_id`, `path`, `kind`, `id`, `classes`, `display` (`none` or `shown`), `border` `{x,y,w,h}`. `path` is the inspector's child-index array, including `kGeneratedPathBit`. An empty path is the canvas root.

`element` and `hit` use the same object:

- `window`, `canvas`, `canvas_id`, `path`, `kind`, `id`, `classes`, `text`, `display` (`none` or `shown`)
- `pseudo`: `hover`, `pressed`, `disabled`, `focus`, `checked`
- boxes `border`, `margin`, `content`, each `{x,y,w,h}`
- `computed`, or `null` when `style_cache_paint_` is invalid. Custom properties are not included
- `motion` (not `motion_shown`): running samples as `{property,value}`
- `bindings`: attribute names, not paths. Only a set binding is listed
- `rules[]`: `selector`, `specificity`, `winner`, `declarations` as `{property,value}`. The last rule has `"winner":true`

`write_rules` calls `match_style_rules` with `window_size_for` (`Presentation.sizes` for that window). Paint fills `style_cache_paint_` with `reference_size` when the canvas is `ScaleWithScreenSize` and both sides are positive. On that canvas, `winner` can be a different `@media` rule than the one that produced `computed`.

When `computed` is an object, `write_element` (`src/cli/cli_commands.cpp`) writes:

| Field | Value |
| --- | --- |
| `color`, `background`, `border_color` | four floats |
| `opacity` | a number |
| `font_size`, `border_width` | a length string |
| `width`, `height` | a length string, omitted when unset |
| `padding`, `margin` | four length strings: top, right, bottom, left |
| `z_index` | an integer |
| `position` | `static`, `relative`, or `absolute` |
| `text_align` | `start`, `center`, or `end` |
| `overflow_x`, `overflow_y` | `visible`, `hidden`, `scroll`, or `auto` |
| `display` | `none` or `shown` |
| `visibility` | `visible` or `hidden` |

A length string is `16px`, `50%`, `2em`, or `"calc(...)"`.

A `motion` `value` is a four-float color, a boolean (`visibility` or `display`), or a number.

`bindings` names: `text`, `content`, `command`, `checked`, `source`, `items`, `drag`, `paint`, `pan-x`, `pan-y`, `zoom`, `scroll-x`, `scroll-y`.

`hit` picks the top non-tool canvas whose rect contains the window point, then calls `hit_test_visual` with layout-local coordinates `(x - offset) / scale` from `canvas_layout_space`. `FillWindow` is 1:1. `ScaleWithScreenSize` is not when `reference_size` is positive on both axes (`offset` is the canvas origin, `scale` is `rect.w / reference_size.x`). A non-positive reference uses the 1:1 path. The game click is not consumed and the cursor does not move.

Every element except `TextInput` runs `element.command`, or `data_context->find_command` when that pointer is null, the command binding is set, and `data_context` is non-null. The call runs when `can_execute()` is true.

A checkbox `click` toggles `checked` first, then takes that command path because a checkbox is not a `TextInput`. The float write happens only when `checked` is bound and `data_context` is non-null. `generated_owner` is preferred, otherwise the canvas `data_context`. A toggle still reports executed when the command is missing or `can_execute()` is false. Does not pick for the inspector and does not move the cursor.

`profile` is `result` with `paused`, `capturing`, `canvases[]`, and `shared`.

Each canvas has `window`, `id` (the document root id), `frames`, `elements`, `generated`, `layout_skipped`, and `stages`. The stage names are `bindings`, `stylesheets`, `input`, `layout`, `motion`, and `paint`. Each stage is `{last_ms,avg_ms,max_ms}` in milliseconds. `shared` has `frames`, plus `begin_frame` and `commands` in that same timing shape.

Selectors: `#id`, `.class`, or `path:` plus the tree path joined by `/`. `path:` alone is the root. `path:0/1` is child 1 of child 0. Several matches are `"error":"ambiguous"` and a `candidates` list; each candidate has the `tree` node's identity fields (`window`, `canvas`, `canvas_id`, `path`, `kind`, `id`, `classes`).

## Canvases

Selectors and `hit` search every canvas on the window unless the request has `canvas`. Two canvases on one window can reuse an id: in the editor the Explorer and Inspector panels both have `#tree` at `path:0/1/0/1`, so neither `#tree` nor its path is one element. `--canvas C` limits `tree`, `element`, `hit`, `click`, and `screenshot` to one canvas, and is a usage error with any other command.

| `--canvas` | Request | Picks |
| --- | --- | --- |
| a non-negative integer | `"canvas":1` | the canvas at that place in the window's draw order: `UiCanvas::order`, then entity index; 0 is drawn first |
| anything else | `"canvas":"inspector"` | every canvas whose document root has that id (`<Canvas id="inspector">`) |

Every `tree` node, `element` / `hit` / `click` result, and `ambiguous` candidate carries `canvas` (the place) and `canvas_id` (the root id, empty when the root has none), so `tree` or the `ambiguous` reply says what to pass. A place shifts when a canvas with a lower place is spawned or destroyed; a root id does not. The editor's documents name their roots `editor`, `explorer`, `inspector`, `profiler`, and `build`.

A `canvas` that names nothing is `no canvas N on window W` or `no canvas "ID" on window W`. A root id shared by two canvases picks both, and a selector that matches on each is still `ambiguous`; pass the place. A `canvas` that is negative, fractional, an empty string, or not a number or string is `invalid request`. `tests/cli_server_test.cpp` covers the parse and two canvases that share `#tree`.

```
wind-cli element "#tree" --window 1 --canvas inspector
wind-cli click "#tree" --window 1 --canvas 2
wind-cli screenshot "#tree" --window 1 --canvas explorer --out tree.png
wind-cli screenshot --window 1 --canvas inspector --out panel.png
```

`click` with `"executed":false` carries `reason`: `disabled`, `no command`, or `can_execute`.

Without `ENGINE_UI_PROFILER` (an exported game's Release), `profile` returns `"UI profiler is not in this build"`.

## Screenshot

`wind-cli screenshot [selector] [--out FILE] [--window N] [--canvas C] [--pid N]` writes one window as this process drew it, as an RGBA PNG. `--out` is relative to the current directory; the default is `screenshot-YYYYMMDD-HHMMSS.png` there. The tool sends the absolute path; the game writes the file, so it lands on the same machine. `--out` with another command is a usage error.

It takes two frames. The first `drain` checks `path` and arms the job. The next frame's `GameLoop` passes one `FrameCapture` per armed window to `IPresentation::draw_all`. `WindowManager::draw_all` calls `OpenGLCanvas::render`, then `read_pixels` (`glReadPixels` of the back buffer, framebuffer 0), then `present`, so the pixels are the ones that frame swaps. `render::framebuffer_image` flips the rows to top first. An opaque window's alpha is 255. A transparent window keeps its alpha, un-premultiplied. The size is the drawable size, the same pixels as `UiCanvas.rect` and `hit`. That frame's `drain` crops, encodes (`encode_png_rgba`), writes, and answers. The encode runs on the main thread.

A selector uses the same matching as `element` (limited to `--canvas` when given) and needs a world on the window. The crop is the element's border box mapped through its canvas's `canvas_layout_space` (`element_window_rect`), grown outward to whole pixels and clipped to the window (`snap_to_pixels`). Without a selector, `--canvas` crops to that canvas's `UiCanvas.rect` (`canvas_window_rect`); a root id shared by two canvases is `ambiguous` with canvas candidates. `rect` is the box in window pixels; with neither it is the whole window. `width` and `height` are the PNG's.

| Error | When |
| --- | --- |
| `screenshot needs an absolute path` | `path` is missing or relative. Answered at once |
| `window N drew nothing: it is closed, hidden, or minimized` | that window did not draw, or `read_pixels` skipped it (`SDL_WINDOW_HIDDEN` or `SDL_WINDOW_MINIMIZED`) |
| `no world on window N` | a selector or `--canvas`, and the window has no world |
| `no canvas …` | `--canvas` names no canvas on the window ([Canvases](#canvases)) |
| `no element`, `ambiguous`, `selector` | as `element` |
| `element is outside the window`, `canvas is outside the window` | the snapped box is empty |
| `could not write PATH`, `could not encode the png` | the file could not be written (the directory must exist) |

`tests/cli_server_test.cpp` covers snapping, a crop of a `Fixed` canvas, a crop to one of two canvases and to an element on it, a `ScaleWithScreenSize` mapping, the refusals, and the two-drain exchange with a fake capture. `tests/game_loop_test.cpp` runs one through `GameLoop` with a presentation that fills the capture. `tests/framebuffer_image_test.cpp` covers the flip and alpha. The `glReadPixels` call needs a GPU and is not in `engine_tests` ([Boundaries](../architecture/Boundaries.md)).

## Dock

`wind-cli dock` drives the dock spaces ([Docking](Docking.md#host)) of the world bound to `--window`: every `DockSpace` entity in it, whatever its own `window` (a float's OS window reaches the same world). Tabs are pressed on chrome, not through an `ICommand`, so `click .dock-tab` cannot switch them; this command can. `--space S` picks one space by its place: lowest `DockSpace::order` first, then entity index. `--space` with another command is a usage error.

```
wind-cli dock [--window N] [--space S]
wind-cli dock activate <panel>
wind-cli dock move <panel> <node> <center|left|right|top|bottom>
wind-cli dock float <panel> [<x> <y> <w> <h>]
wind-cli dock mode <virtual|os>
```

| Request | Does |
| --- | --- |
| `{"command":"dock"}` | Lists the spaces (all, or the one `space` names) |
| `"action":"activate","panel":K` | `DockLayout::activate`. A panel in a virtual float also raises that float (`raise_float`), as a press on its tab does; an OS window float is not raised |
| `"action":"move","panel":K,"node":N,"zone":Z` | `DockLayout::move` to `DockTarget{N, Z}`. Node 0 is the dock area. `zone` defaults to `center` |
| `"action":"float","panel":K` | `DockLayout::float_panel`. The frame is `x`, `y`, `w`, `h` (window pixels; all four, `w` and `h` positive), or `metrics.float_size` centered in `area` |
| `"action":"mode","mode":M` | `DockSpace::float_mode`: `virtual` or `os`. The floats convert on the next layout pass |

A layout change goes through the `DockLayout` operation and bumps `DockSpace::revision` once when the layout differs afterwards, as the dock system does for a user's change, so a host that saves on a new revision saves it. No difference (the tab is already active, `move` to Center of its own stack) is `"changed":false` and keeps the revision. `mode` does not touch the layout and does not bump it. The command answers in `drain`, after the frame drew; the next frame's layout pass places the canvases, so a `tree`, `hit`, or `screenshot` sent after the answer sees the change.

A panel action takes the one space whose layout holds `panel`; `mode` takes the only space. More than one is `"error":"ambiguous"` with `candidates` (`space`, `window`, `order`); pass `--space`.

The list is `result.spaces[]`, each:

| Field | Value |
| --- | --- |
| `space`, `window`, `order` | Its place, `DockSpace::window`, `DockSpace::order` |
| `area` | `{x,y,w,h}` |
| `float_mode` | `virtual` or `os` |
| `revision` | `DockSpace::revision` |
| `gesture` | True while a pointer gesture (tab press or drag, splitter, float move or resize) is in progress |
| `root` | The docked root's node id, 0 when empty |
| `nodes[]` | `id`, `parent`, `float` (0 when docked), `kind`. `tabs`: `panels` and `active` (a key). `split`: `axis` (`horizontal`, `vertical`), `ratio`, `first`, `second` |
| `floats[]` | Bottom to top: `id`, `root`, `rect` (the stored frame), `os_window` (its window id, or null) |
| `panels[]` | The layout's panels (docked tree depth first, then floats), then registered panels the layout lacks: `key`, `registered`, `title`, `closable`, `stack` and `index` (null when not in the layout), `float`, `visible` (the active tab of its stack), `os_window` (`dock_panel_os_window`) |
| `layout` | `dock_layout_to_text`: the TOML a host saves |

A change answers `space`, `action`, `changed`, `revision`, `float_mode`, and `layout` (the text after the change).

| Error | When |
| --- | --- |
| `no dock space on window N` | the world has no `DockSpace` |
| `no dock space S on window N` | `space` is past the last place |
| `no panel K` | no space's layout holds `panel` |
| `X needs a panel` | `activate`, `move`, or `float` without `panel` |
| `no node N` | `move` to a node the layout lacks |
| `unknown zone Z`, `unknown float mode M`, `unknown dock action X` | as named |
| `float needs x, y, and a positive w and h` | part of a frame, or an empty one |
| `a dock gesture is in progress` | the space is in a gesture; a change would race it. The list still answers |

`tests/cli_dock_test.cpp` covers the parse, the list (nodes, floats, panels, a registered panel the layout lacks, the text), each action with its revision and the next layout pass, the refusals, two spaces picked by panel or place, a gesture in progress, and one exchange through `drain`.

## Host commands

A command that is not a UI command goes to `RunHooks::cli.handle` with `CliCommand{name, path}`, on the main thread inside `drain`, after the frame drew and before `on_frame_end`. A host records what to do there and acts in `on_frame_end`, like a toolbar button. `begin_frame` leaves host commands for `drain`.

| `handle` returns | Body |
| --- | --- |
| nullopt, or no `handle` | `{"ok":false,"error":"unknown command"}` |
| `CliReply` with `ok` false | `{"ok":false,"error":error}` |
| `CliReply` with `ok` true | `{"ok":true,"result":{…}}`, the fields in order |

A field is a `CliValue`: `std::monostate` (null), `bool`, `std::int64_t`, `double` (`{:.6g}`, null when not finite), or `std::string` (escaped). The server writes the JSON, so the host does not. A game leaves `RunHooks::cli` empty.

## Editor commands

The editor's `RunHooks::cli` has `kind` `editor` and calls `EditorCli` ([Editor](Editor.md#wind-cli)).

| Command | Result |
| --- | --- |
| `state` | `run` (`idle`, `building`, `playing`), `playable`, `status` (the status line), `project`, `project_dir`, `sdk`; null when there is none |
| `play` | `requested`: `play`. Refused with `already building` / `already playing`, or `not playable: <status line>` |
| `stop` | `requested`: `stop`, `was`: `building` or `playing`. A build is cancelled. Refused with `not playing` |
| `open <project>` | `requested`: `open`, `path`. The tool sends the absolute path; `wind_project.toml` is taken as its directory. Refused while building or playing. The editor has no Open dialog: this is how a running editor switches projects |

`play` and `stop` answer before anything happens: the editor acts in `on_frame_end` of the same frame, so the next `state` already shows `building` (or `idle` with the reason in `status`).

`wind-cli play --wait [S]` sends `play`, then polls `state` every 250 ms. It prints the last `state` and exits 0 on `playing`, 1 on `idle` (the build or Play failed, or the game quit) or after `S` seconds (default 600).

`wind-cli launch <project> [--play [--wait [S]]] [--editor PATH]` starts `wind_editor` with `--project <dir>` (and `--play`) the way the launcher does: detached, in the editor's directory, out of the terminal's job when the job allows it. The editor is `wind_editor` beside `wind-cli` (the SDK's `bin/`) unless `--editor` names one. `<project>` is a directory or its `wind_project.toml`, relative to the current directory; it must hold `wind_project.toml`. The tool waits up to 30 s for the editor's descriptor and fails if the editor exits first. It prints `{"ok":true,"result":{"pid":…,"port":…}}`. With `--play --wait` it then waits as `play --wait` does and adds `state`; on failure the body has `"ok":false` and `error` set to the status line. `launch` is Windows only; elsewhere it says so and exits 1.

`--play` and `--editor` outside `launch`, and `--wait` outside `play` and `launch --play`, are usage errors (exit 2).

## See also

- [UI Inspector](UI%20Inspector.md)
- [UI Profiler](UI%20Profiler.md)
- [Boundaries](../architecture/Boundaries.md)
- [Core](../modules/Core.md)
