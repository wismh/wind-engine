---
tags: [architecture]
---

# Principles

Normative rules. If a change fights these, the change is wrong. As-built detail lives in modules and features; if that prose drifts from code, update the vault.

## Goals

1. **Low coupling.** Core services do not construct each other; Boost.DI injects constructors. No service locator (`Engine::get_audio()`).
2. **Clear ownership.** Engine owns window / GL / audio / import. Game owns the `assets/` tree, `.meta` files, and generated `asset_ids.h`.
3. **One way to draw.** Game and ECS never call OpenGL. They push `Command`s; the backend executes them.
4. **Named input.** Gameplay binds controls → interned `ActionId`, not raw keys in systems.
5. **Assets only by GUID.** `AssetsDb::get<T>(AssetId)` (fatal if missing cooked asset) or `try_get`. Named atlas frames: `get_sprite(id, name)`. No filenames in game code.
6. **Import settings live in `.meta`.** A bare PNG/WAV is not a texture/sound until its sidecar says how to load it.
7. **Audio is a system, not a filename firehose.** `IAudioSystem` plays `Sound` objects from the audio importer, not `PlaySoundEvent{"hit.wav"}`.
8. **Reusable across many production games.** Window title/size come from `IGame`. Do not size features to one title.
9. **Test the engine, not the games built with it.** Shared logic has GoogleTest coverage here. Gameplay stays in the game repo.
10. **Simulation is fixed-step.** Frame time drives present and audio fades; gameplay/physics tick at a constant `fixed_delta_time`.
11. **UI is document + style + VM.** XML and the C++ `ui::Node` builder produce the same `Element` tree. Style is CSS. UI → game is `ViewModel` / `ICommand` only — no `onClick`.
12. **Draw with materials, then sort.** `Renderable` is mesh + material + layer, not ad-hoc shader/texture pointers with undefined order.

## Do not regress

1. Game logic does not include glad / SDL render / mixer / NanoVG / spdlog / tinyxml2. Host includes `engine.h`. Game targets do not add `engine/src` to their include path.
2. Draw only through `CommandBuffer`. The public variant has **no** custom GL callback. World draws use `IMaterial`. UI custom primitives go through `IDrawList` from `IPaint`.
3. Load only through `AssetsDb` by `AssetId`. Gameplay uses `get` (fatal). Optional content uses `try_get` and handles `AssetError`.
4. Play audio only through `IAudioSystem` with a `Sound` from the audio importer (or a test double).
5. Simulation / gameplay cross-talk: **event queues** (`send` / `read` / `flush_events`), not observer `Subscribe`. **UI → game:** `ICommand` on a `ViewModel` only.
6. Shared engine behavior ships with a GoogleTest, not only a game that “seems to work”.
7. Do not change an asset GUID after it is referenced. Move files with their `.meta`. Builtin GUIDs in `builtin_ids.h` are frozen.
8. `asset_codegen` never writes `.meta`. Missing sidecar is a **failed build**, not a random GUID in CI.
9. ECS is homemade, EnTT-shaped. **Do not add EnTT as a submodule.** Do not keep a Node graph beside World. No `Transform` parent.
10. Simulation uses `fixed_delta_time` on `Schedule::Fixed`. One-shot clicks run on `Schedule::Frame`, `Phase::Game`.
11. UI documents are XML assets and/or `ui::Node` into the same `Element` tree. Games do not build a parallel widget graph.
12. All engine APIs: **main thread only.**
13. `MouseConsumed` is cleared at the start of each Loop iteration, not at the end.
14. No engine code assumes a single global window. A `UiCanvas` (and world `Renderable`s, which currently draw only into `kPrimaryWindow`) is keyed by `WindowId`. Window size is `ctx<WindowSizes>()`, not a singleton “the window”.
15. Window / GL / SDL platform calls stay behind `WindowManager` in `src/render/opengl/`. `#if defined(_WIN32)` branches do not leak a Win32 type into `include/`.

## Constraints

- CMake ≥ 3.15, C++23 (MSVC 2022 / clang / gcc).
- SDL3 + SDL3_mixer (WAV only in this mixer build; no OGG).
- OpenGL 3.3 Core via glad; shaders GLSL 330 wrapped in XML `.shader`.
- `.meta` files are TOML (tomlplusplus).
- `ASSETS_PATH` is resolved from the executable directory, not the process cwd.
- Asset GUIDs: exactly 32 lowercase hex chars; stable once referenced. Never reuse a GUID.
- Logging: spdlog only in `src/`. Public facade `engine::log::{info,warn,error}`.
- Fatal errors: `IFatalError` hook — not C++ exceptions for normal gameplay.
- Public headers may include glm. They must not include SDL, glad, NanoVG, spdlog, or mixer.

## See also

- [[architecture/Scope]]
- [[architecture/Boundaries]]
- [[architecture/Overview]]
