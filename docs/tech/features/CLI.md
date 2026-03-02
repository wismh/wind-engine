---
tags: [feature]
---

# CLI

`wind-cli` is a host tool. It does not link `engine`. The game process, when `ENGINE_CLI_SERVER` is on, listens on `127.0.0.1` and answers one route, `POST /exec`, with a JSON body `{"command":"..."}`. The tool prints that JSON and exits 0 when it starts with `{"ok":true`. A command error is still HTTP 200 with `"ok":false`. Usage errors exit 2.

Debug and RelWithDebInfo define `ENGINE_CLI_SERVER`. Release and MinSizeRel do not, and neither do Emscripten or Android. Without the macro the game does not open a port and does not write a descriptor. `wind-cli` still builds. There is no header under `include/engine/`. The server lives in [[src.cli.cli_server.h]] and [[src.cli.cli_server.cpp]]. Commands live in [[src.cli.cli_commands.cpp]].

`kApiEpoch` stays 4.

## Wire

[[src.core.game_loop.cpp]] calls `cli::start` from `GameLoop::begin` and `cli::stop` from `GameLoop::end`. `begin_frame` and a test do not bind a port on their own. The OS picks the port (`bind` with port 0). A descriptor is written for the current user only and removed on stop:

- Windows: `%LOCALAPPDATA%\wind\cli\<pid>.json`
- Otherwise `$XDG_RUNTIME_DIR/wind/cli`, or `/tmp/wind-cli-<uid>`

Fields are `pid`, `port`, `token`, and `exe`. The token is 32 random bytes, base64. Every request sends `Authorization: Bearer`. A request with an `Origin` header is rejected (403) before the token is checked. A missing or wrong token is 401. Anything other than `POST /exec` is 404. The accept thread queues the body and waits. Engine calls run on the main thread. The response timeout is five seconds (504). Stopping the server answers a waiter with 503.

Queries and `profile` run in `cli::drain`, after `draw_all` in `tick` and `reentrant_tick`, so style and boxes are from this frame. `click` is armed on that drain and runs at the start of the next frame, before simulate. The response is the drain after that, so the click has already been painted. `execute()` itself runs a click immediately. The deferral is only on the socket path. [[tests.cli_server_test.cpp]] pumps the queue on the test thread. It does not go through `GameLoop`.

## Commands

`status` lists live descriptors and does not contact the game. It does not print the token. A dead pid's file is removed. One live game is the target. Several require `--pid`. `--window` selects a `WindowId` (default 0, `kPrimaryWindow`).

- `tree` — flat list of every canvas on that window except an `InspectorPanel` or `ProfilerPanel`. Each node has `path`, `kind`, `id`, `classes`, `display`, and the border box. `path` is the same child-index array the inspector stores, including `kGeneratedPathBit` ([[src.ui.element_path.h]]). An empty path is the canvas root.
- `element <selector>` — the same detail the inspector panel builds: kind, id, classes, text, pseudos, the three boxes, computed style from `style_cache_paint_`, running `motion_shown`, bindings as the names that are bound, and the matched rules. The last rule has `"winner":true`. A selector is `#id`, `.class`, or `path:` plus the tree path joined by `/` (`path:` alone is the root, `path:0/1` is child 1 of child 0). Several matches are `"error":"ambiguous"` and a `candidates` list.
- `hit <x> <y>` — `hit_test_visual` in window coordinates, on the top non-tool canvas whose rect contains the point. A miss is `"result":null`. The game click is not consumed and the cursor does not move.
- `click <selector>` — the button and checkbox path from `handle_pointer`: a checkbox toggles and writes its bound float; any other element except `TextInput` runs `element.command` or `find_command` when `can_execute()` is true. It does not pick for the inspector and does not move the cursor. `"executed":false` carries `reason`: `disabled`, `no command`, or `can_execute`.
- `profile` — last, average, and max per stage, in milliseconds, for each non-tool canvas (bindings, stylesheets, input, layout, motion, paint, plus elements, generated, and whether layout was skipped) and the shared `begin_frame` and `commands` rows. `paused` is the panel pause. See [[features/UI Profiler]].
- `profile stop` — clears CLI capture only. It does not close the profiler window.

Without `ENGINE_UI_PROFILER`, `profile` returns `"UI profiler is not in this build"`.

## See also

- [[features/UI Inspector]]
- [[features/UI Profiler]]
- [[architecture/Boundaries]]
- [[modules/Core]]
- [[modules/UI]]
