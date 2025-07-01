# Wind technical documentation

These pages describe the Wind engine as the code implements it. If a page disagrees with code, code wins and the page should be updated in the same change.

Product name: Wind. CMake target and C++ namespace: `engine`. Build instructions for this repo stay in the root [README](../../README.md).

## How to read this

1. [Principles](architecture/Principles.md), [Scope](architecture/Scope.md), and [Boundaries](architecture/Boundaries.md) are the rules.
2. [Overview](architecture/Overview.md) and [Module Map](architecture/Module%20Map.md) say which area owns what.
3. [Runtime Loop](architecture/Runtime%20Loop.md) is the frame order.
4. A module page is the reference for that area. A feature page is a walkthrough of one subsystem. A build page is how a binary and its assets get onto disk.
5. [File index](files/INDEX.md) lists every first-party file under `include/engine/`, `src/`, `tests/`, `tools/`, and `editor/`.

[UI Performance Plan](architecture/UI%20Performance%20Plan.md) and [Editor Plan](architecture/Editor%20Plan.md) are plans, not descriptions of the current engine.

## Architecture

| Page | What it is |
| --- | --- |
| [Principles](architecture/Principles.md) | Normative rules |
| [Scope](architecture/Scope.md) | In scope, backlog, names |
| [Boundaries](architecture/Boundaries.md) | Public headers, compile flags, what `engine_tests` may do |
| [Overview](architecture/Overview.md) | What a game sees |
| [Module Map](architecture/Module%20Map.md) | Who talks to whom |
| [Runtime Loop](architecture/Runtime%20Loop.md) | Init and one frame |
| [UI Performance Plan](architecture/UI%20Performance%20Plan.md) | Planned UI work, labeled as a plan |
| [Editor Plan](architecture/Editor%20Plan.md) | Planned editor host and game module, labeled as a plan |

## Modules

| Page | Area |
| --- | --- |
| [Core](modules/Core.md) | Host, time, input, log, windows, fatal errors |
| [ECS](modules/ECS.md) | World, schedules, events, camera, physics probe |
| [Resources](modules/Resources.md) | GUIDs, `.meta`, `AssetsDb`, codegen |
| [Render](modules/Render.md) | Materials, commands, sort, OpenGL, NanoVG |
| [UI](modules/UI.md) | Documents, CSS, layout, input, paint, MVVM |
| [Localization](modules/Localization.md) | String tables and `{tr}` |
| [Audio](modules/Audio.md) | SFX pool, music, looping handles |
| [Haptics](modules/Haptics.md) | Duration and intensity vibration |
| [Net](modules/Net.md) | HTTP requests owned by the caller |
| [Process](modules/Process.md) | Child processes owned by the caller, independent programs |
| [Project](modules/Project.md) | `wind_project.toml` and `sdk.toml` |

## Features

| Page | Walkthrough |
| --- | --- |
| [Assets](features/Assets.md) | Catalog, importers, `get` / `try_get` |
| [Materials and Sort](features/Materials%20and%20Sort.md) | Materials, sprites, draw order |
| [Windowing](features/Windowing.md) | Windows, overlay, click-through |
| [Input Mapper](features/Input%20Mapper.md) | `ActionId` bindings |
| [UI Markup](features/UI%20Markup.md) | XML, CSS, builder, bind |
| [UI Input](features/UI%20Input.md) | Pointer, keys, text, scroll |
| [UI Inspector](features/UI%20Inspector.md) | Pick and tree probe; the editor's Inspector tab |
| [UI Profiler](features/UI%20Profiler.md) | Per-stage timings; the editor's Profiler tab |
| [CLI](features/CLI.md) | `wind-cli` loopback server |
| [Editor](features/Editor.md) | `wind_editor`: choose a game module, Play, Stop |

## Build

| Page | What it covers |
| --- | --- |
| [Pipeline](build/Pipeline.md) | Configure, codegen, compile, copy assets, run |
| [CMake](build/CMake.md) | Options, presets, `engine_add_game` |
| [Asset Codegen](build/Asset%20Codegen.md) | `asset_codegen` and `asset_guid` |
| [Icon Codegen](build/Icon%20Codegen.md) | `icon.png` to platform icons |
| [Runtime Assets](build/Runtime%20Assets.md) | Where the running process reads assets |
| [Game Consumer](build/Game%20Consumer.md) | A game repo that builds against an installed SDK (`find_package(Wind)`) |
