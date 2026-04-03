# CLI

`wind-cli` is a host tool. It does not link `engine`. When `ENGINE_CLI_SERVER` is on, the game listens on `127.0.0.1` and answers `POST /exec`. The tool prints the JSON body and exits 0 when that body starts with `{"ok":true`. A body that does not start that way exits 1, as do a missing game and a failed exchange. `status` always exits 0.

A command error is still HTTP 200 with `"ok":false`. Usage errors exit 2.

Debug and RelWithDebInfo define `ENGINE_CLI_SERVER`, and not on Emscripten or Android. Release and MinSizeRel do not. Without the macro the game does not open a port and does not write a descriptor. `wind-cli` still builds. There is no header under `include/engine/`.

| File | Role |
| --- | --- |
| `src/cli/cli_server.h` | request types, `start` / `stop` / `begin_frame` / `drain` |
| `src/cli/cli_server.cpp` | socket, descriptor, accept thread. Empty translation unit without the macro |
| `src/cli/cli_commands.cpp` | `tree`, `element`, `hit`, `click`, `profile` |
| `tools/wind_cli/main.cpp` | the host client |

The tool does not check the engine build id.

## When it runs

`GameLoop::begin` calls `cli::start`. `GameLoop::end` calls `cli::stop`. `begin_frame` does not bind a port. A test that calls `begin_frame` does not either.

`tick` and `reentrant_tick`:

1. `cli::begin_frame` runs an armed `click` before `flush_events` and simulate.
2. After `draw_all`, `cli::drain` answers `tree`, `element`, `hit`, and `profile` from this frame's painted tree, and arms a `click` for the next `begin_frame`.

`execute()` inside the command runs a click immediately. The deferral is only the socket path, so the response is the drain after the click has been painted. `tests/cli_server_test.cpp` pumps the queue on the test thread. It does not go through `GameLoop`.

## Wire

The OS picks the port (`bind` with port 0). A descriptor is written for the current user and removed on stop.

| Platform | Directory |
| --- | --- |
| Windows | `%LOCALAPPDATA%\wind\cli\<pid>.json` |
| `XDG_RUNTIME_DIR` set | `$XDG_RUNTIME_DIR/wind/cli` |
| otherwise | `/tmp/wind-cli-<uid>` |

Fields: `pid`, `port`, `token`, `exe`. The token is 32 random bytes, base64. Every request sends `Authorization: Bearer`.

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

`status` lists live descriptors and does not contact the game. It does not print the token. A dead pid's file is removed. One live game is the target. Several require `--pid`. `--window` selects a `WindowId` (default 0, `kPrimaryWindow`).

The JSON body is `{"command":"…"}` plus optional `selector`, `window`, `x`, and `y`. `profile stop` (and `--stop`) also sends `"stop":true`.

| Command | Result |
| --- | --- |
| `tree` | `result.nodes[]` for every canvas on that window |
| `element <selector>` | One element object (fields below) |
| `hit <x> <y>` | That same object, or `"result":null` |
| `click <selector>` | `executed`, and `reason` when it did not run |
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

Without `ENGINE_UI_PROFILER`, `profile` returns `"UI profiler is not in this build"`.

## See also

- [UI Inspector](UI%20Inspector.md)
- [UI Profiler](UI%20Profiler.md)
- [Boundaries](../architecture/Boundaries.md)
- [Core](../modules/Core.md)
