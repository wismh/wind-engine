# CLI

`wind-cli` is a host tool. It does not link `engine`. When `ENGINE_CLI_SERVER` is on, the game or the editor listens on `127.0.0.1` and answers `POST /exec`. The tool prints the JSON body and exits 0 when that body starts with `{"ok":true`. A body that does not start that way exits 1, as do a missing game and a failed exchange. `status` always exits 0. `launch` starts an editor ([Editor commands](#editor-commands)).

A command error is still HTTP 200 with `"ok":false`. Usage errors exit 2.

Debug, RelWithDebInfo, and every configuration of the editor build (`ENGINE_EDITOR`, so the Release editor too) define `ENGINE_CLI_SERVER`, and not on Emscripten or Android. An exported game's Release and MinSizeRel do not. Without the macro the game does not open a port and does not write a descriptor. `wind-cli` still builds. The only public header is `<engine/core/cli_commands.h>`, for a host's own commands ([Host commands](#host-commands)). The SDK installs `wind-cli` into `bin/`, beside `wind_editor`.

| File | Role |
| --- | --- |
| `include/engine/core/cli_commands.h` | `CliCommand`, `CliReply`, `CliCommands`: the host's commands, passed in `RunHooks::cli` |
| `src/cli/cli_server.h` | request types, `CliFrame`, `start` / `stop` / `begin_frame` / `drain` |
| `src/cli/cli_server.cpp` | socket, descriptor, accept thread, routing. Empty translation unit without the macro |
| `src/cli/cli_commands.cpp` | `tree`, `element`, `hit`, `click`, `profile`, `element_window_rect`, and the JSON of a host reply |
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
3. After `draw_all`, `cli::drain` answers `tree`, `element`, `hit`, and `profile` from this frame's painted tree, answers an armed `screenshot` from `CliFrame::captures`, arms a `click` for the next `begin_frame` and a new `screenshot` for the next `draw_all`, and passes every other command to the host.

A UI command (`tree`, `element`, `hit`, `click`, `profile`) runs against the world bound to the request's `window` (default 0, `kPrimaryWindow`). When that window has no world, the next `drain` answers `{"ok":false,"error":"no world on window N"}` at once; there is no 504. In the editor between plays `kPrimaryWindow` has no world; `--window` with the editor window's id reaches the editor's own canvases.

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

`status` lists live descriptors (`pid`, `port`, `kind`, `exe`) and does not contact the process. It does not print the token. A dead pid's file is removed. One live process is the target of a UI command. Several require `--pid`. The editor commands consider only `kind` `editor`, so a standalone game beside the editor does not need `--pid`. `--window` selects a `WindowId` (default 0, `kPrimaryWindow`).

The JSON body is `{"command":"…"}` plus optional `selector`, `window`, `x`, `y`, and `path` (absolute, UTF-8). `profile stop` (and `--stop`) also sends `"stop":true`.

| Command | Result |
| --- | --- |
| `tree` | `result.nodes[]` for every canvas on that window |
| `element <selector>` | One element object (fields below) |
| `hit <x> <y>` | That same object, or `"result":null` |
| `click <selector>` | `executed`, and `reason` when it did not run |
| `screenshot [selector]` | `path`, `window`, `width`, `height`, `rect`. Writes a PNG ([Screenshot](#screenshot)) |
| `profile` | `result` timings below. `paused` is the editor panel's Pause |
| `profile stop` | `result.capturing` is false. Clears CLI capture only. Does not detach the editor's profiler panel |

`tree` nodes: `window`, `path`, `kind`, `id`, `classes`, `display` (`none` or `shown`), `border` `{x,y,w,h}`. `path` is the inspector's child-index array, including `kGeneratedPathBit`. An empty path is the canvas root.

`element` and `hit` use the same object:

- `window`, `path`, `kind`, `id`, `classes`, `text`, `display` (`none` or `shown`)
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

Selectors: `#id`, `.class`, or `path:` plus the tree path joined by `/`. `path:` alone is the root. `path:0/1` is child 1 of child 0. Several matches are `"error":"ambiguous"` and a `candidates` list.

`click` with `"executed":false` carries `reason`: `disabled`, `no command`, or `can_execute`.

Without `ENGINE_UI_PROFILER` (an exported game's Release), `profile` returns `"UI profiler is not in this build"`.

## Screenshot

`wind-cli screenshot [selector] [--out FILE] [--window N] [--pid N]` writes one window as this process drew it, as an RGBA PNG. `--out` is relative to the current directory; the default is `screenshot-YYYYMMDD-HHMMSS.png` there. The tool sends the absolute path; the game writes the file, so it lands on the same machine. `--out` with another command is a usage error.

It takes two frames. The first `drain` checks `path` and arms the job. The next frame's `GameLoop` passes one `FrameCapture` per armed window to `IPresentation::draw_all`. `WindowManager::draw_all` calls `OpenGLCanvas::render`, then `read_pixels` (`glReadPixels` of the back buffer, framebuffer 0), then `present`, so the pixels are the ones that frame swaps. `render::framebuffer_image` flips the rows to top first. An opaque window's alpha is 255. A transparent window keeps its alpha, un-premultiplied. The size is the drawable size, the same pixels as `UiCanvas.rect` and `hit`. That frame's `drain` crops, encodes (`encode_png_rgba`), writes, and answers. The encode runs on the main thread.

A selector uses the same matching as `element` and needs a world on the window. The crop is the element's border box mapped through its canvas's `canvas_layout_space` (`element_window_rect`), grown outward to whole pixels and clipped to the window (`snap_to_pixels`). `rect` is that box in window pixels; without a selector it is the whole window. `width` and `height` are the PNG's.

| Error | When |
| --- | --- |
| `screenshot needs an absolute path` | `path` is missing or relative. Answered at once |
| `window N drew nothing: it is closed, hidden, or minimized` | that window did not draw, or `read_pixels` skipped it (`SDL_WINDOW_HIDDEN` or `SDL_WINDOW_MINIMIZED`) |
| `no world on window N` | a selector, and the window has no world |
| `no element`, `ambiguous`, `selector` | as `element` |
| `element is outside the window` | the snapped box is empty |
| `could not write PATH`, `could not encode the png` | the file could not be written (the directory must exist) |

`tests/cli_server_test.cpp` covers snapping, a crop of a `Fixed` canvas, a `ScaleWithScreenSize` mapping, the refusals, and the two-drain exchange with a fake capture. `tests/game_loop_test.cpp` runs one through `GameLoop` with a presentation that fills the capture. `tests/framebuffer_image_test.cpp` covers the flip and alpha. The `glReadPixels` call needs a GPU and is not in `engine_tests` ([Boundaries](../architecture/Boundaries.md)).

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
