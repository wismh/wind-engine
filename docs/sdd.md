# Engine Software Design Document


| Field     | Value                                                                                |
| --------- | ------------------------------------------------------------------------------------ |
| Document  | SDD-WIND-001                                                                         |
| Project   | **Wind** — a small, embeddable 2D C++ game engine                                    |
| Status    | Design of record for the **target** standalone repo                                  |
| Language  | C++23                                                                                |
| Tests     | GoogleTest, target `engine_tests` (see §12)                                          |
| Consumers | Sibling game repos via git submodule                                                |


This document is the **design of record** for the engine. Implementation notes that are still open live in §17.

---



## 1. Purpose and scope



### 1.1 Purpose

A small real-time 2D engine (**Wind**): window, input, ECS, command-buffer rendering (OpenGL + **materials** + **sort**), NanoVG UI from **XML + CSS + C++ MVVM**, assets, audio. Games are thin `IGame` implementations. Wind is a **static library** in its own git repo; games do not vendor SDL or copy engine sources.

### 1.2 In scope

- Lifecycle: init, main loop, shutdown.
- Window, GL context, input actions, mouse → UI.
- One `ecs::World` (no Node scene graph). UI is an ECS component (`UiCanvas` + XML document + ViewModel), not a parallel tree and not a C++ widget graph built in game code.
- Homemade ECS with **EnTT as API/implementation reference only** (not a dependency): generational entities, `try_get`, views.
- Render abstractions + OpenGL 3.3 backend: **materials**, instance tint, **layer sort**; NanoVG executes `CmdDrawUI` from the bound XML tree.
- AssetsDb + GUID catalog: sidecar TOML `.meta`, `asset_guid` + `asset_codegen` (see §10).
- Audio: buses, SFX pool, music A/B, looping SFX handles (Lumenwake-shaped API on SDL3_mixer).
- Double-buffered **event queues** (Bevy `Events<T>` shape), not a callback bus.
- Boost.DI wiring of engine services.
- GoogleTest suite for engine logic (`engine_tests`, `ctest`).



### 1.3 Out of scope

- Gameplay, levels, AI, menus of a specific game.
- 3D renderer, lighting, skeletal animation.
- Physics engine (Box2D / rapier). Current physics is AABB + velocity integration only.
- Networking, scripting VM, editor, asset pipeline GUI.
- DirectX / Metal backends (interfaces exist so they *can* be added).
- GPU / window golden-image tests and full `Engine<GameT>` boot in CI (no display on some agents).
- Browser-grade CSS (Grid, `calc()`, `%`/`em`, media queries, animations, `@font-face` as in a browser).
- Full WPF: `ControlTemplate`, `VisualStateManager`, attached DPs, `x:Class` code-behind, C++ reflection / Qt moc.
- Parent/child `Transform` hierarchy (v1 is a flat list of world entities).



### 1.4 Names


| Name            | Role                                                       |
| --------------- | ---------------------------------------------------------- |
| Wind            | Product name of this engine                                |
| `wind-N`        | Task code on commits and `feat/wind-N-…` branches          |
| engine          | CMake static library and C++ namespace                     |
| `engine_tests`  | GoogleTest binary (`GTest::gtest_main`)                    |
| `IGame`         | Game-facing lifecycle contract                             |
| `Engine<GameT>` | Template host that constructs DI graph and runs `Loop`     |
| `asset_guid`    | Dev tool: create missing `.meta` + new GUIDs (writes the assets tree)               |
| `asset_codegen` | Build tool: **read-only** scan; emit `asset_ids.h` + cooked catalog; fail if meta missing |
| `AssetId`       | 32-char lowercase hex GUID, strong type                                            |
| `try_get` / `get`| Optional vs fatal asset lookup (see §10.7)                                         |
| `IMaterial`     | Shader + texture slots + blend + default color (see §6.2)                          |
| `ViewModel`     | Game C++ MVVM object; XML binds to registered names (see §8)                       |
| `ICommand`      | UI → VM (WPF command), not `onClick` lambdas in game code                          |
| `UiDocument`    | Parsed XML view (`importer = "ui"`)                                                |
| `StyleSheet`    | Parsed custom CSS (`importer = "css"`)                                             |
| `WindowId`      | Strong handle for one OS window; `kPrimaryWindow` is the game's first window (see §21) |
| `WindowDesc` / `WindowStyle` | Public, GL/SDL-free description of a window's title/size/position and borderless/always-on-top/transparent flags (§21.2) |
| `WindowManager` | Private (`src/render/opengl/…`) owner of every `WindowSystem` + `OpenGLCanvas` + `CommandBuffer` pair, one per `WindowId` (§21.5) |


---



## 2. Goals, non-goals, constraints



### 2.1 Goals

1. **Low coupling.** Core services do not construct each other; Boost.DI injects constructors.
2. **Clear ownership.** Engine owns window/GL/audio/import; game owns the `assets/` tree, `.meta` files, and generated `asset_ids.h`.
3. **One way to draw.** Game and ECS never call OpenGL. They push `Command`s; the backend executes them.
4. **Named input.** Gameplay binds controls → interned `ActionId`, not raw keys in systems.
5. **Assets only by GUID.** `AssetsDb::get<T>(AssetId)` (fatal if missing cooked asset) or `try_get`. No filenames in game code.
6. **Import settings live in `.meta`.** A bare PNG/WAV is not a texture/sound until its sidecar says how to load it (color space, filter, sound bank, …).
7. **Audio is a system, not a filename firehose.** `IAudioSystem` plays `Sound` objects produced by the audio importer, not `PlaySoundEvent{"hit.wav"}`.
8. **Reusable across games.** Window title/size come from `IGame`; audio and render APIs stay game-agnostic.
9. **Test the engine, not the games built with it.** Logic that will be shared (ECS, events, commands, audio policy, meta/catalog, input, camera, fixed-step loop) has GoogleTest coverage in this repo. Gameplay stays in the game repo.
10. **Simulation is fixed-step.** Frame time drives present and audio fades; gameplay/physics tick at a constant `fixed_delta_time` (§4.4).
11. **UI is markup + style + VM.** Games do not build `UIElement` trees in C++. XML + custom CSS + `ViewModel` / `ICommand` (see §8).
12. **Draw with materials, then sort.** `Renderable` is mesh + material + layer, not ad-hoc shader/texture pointers with undefined order (§6).



### 2.2 Non-goals (v1)

- Hot reload.
- 3D spatial audio, Doppler, HRTF.
- JSON/ScriptableObject sound banks in C++ (`GameSounds { … }` with hardcoded volume). Volume/pitch live in audio `.meta`.
- Pitch re-roll every looping-SFX cycle (Lumenwake `LateUpdate` trick) — API may appear later.
- Sharing one process between multiple games (multiple `IGame` instances). Multiple **windows** for one running game is in scope — see §21.
- Building or mutating visual trees from game C++ as the supported UI API (tests may construct trees).
- Transform parenting, scene-graph matrices, or auto Y-sort unless a later `sort_mode` is added.
- Per-pixel (framebuffer-alpha) click-through. v1 click-through is bounding-box hit-test only (§21.4).



### 2.3 Constraints

- CMake ≥ 3.15, C++23 (MSVC 2022 / clang / gcc).
- SDL3 + SDL3_mixer (WAV only in this mixer build; no OGG).
- OpenGL 3.3 Core via glad; shaders GLSL 330 wrapped in XML `.shader`.
- ECS: own `ecs::World`, modeled on EnTT’s contract (see §7). **Do not link EnTT.**
- `.meta` files are **TOML** (tomlplusplus).
- `ASSETS_PATH` is resolved from the **executable directory** (e.g. `SDL_GetBasePath()`), not the process cwd.
- Asset GUIDs: exactly 32 lowercase hex chars; stable once referenced. Never reuse a GUID.
- Logging: spdlog **only in `src/`**. Public facade `engine::log::{info,warn,error}` (no spdlog types in `include/`). File: `<exe dir>/game.log`.
- Fatal errors (missing cooked asset, corrupt catalog): `IFatalError` hook — game shows a system dialog and quits; tests fail the assertion. Not C++ exceptions for normal gameplay.
- Tests: GoogleTest (same pattern as Q+: `gtest_force_shared_crt`, `INSTALL_GTEST OFF`). `enable_testing()` + `gtest_discover_tests`.
- **Main thread only.** `World`, GL, mixer, `AssetsDb`, UI bindings — not thread-safe. Do not call engine APIs from worker threads.
- Public headers may include **glm**. They must **not** include SDL, glad, NanoVG, spdlog, or mixer. `tinyxml2` is private (shader + UI XML parse in `src/`).

---



## 3. Repository and how games consume it



### 3.1 Layout (this repo)

```
engine/
  CMakeLists.txt          # deps + static `engine`; optional `engine_tests`
  docs/sdd.md             # this file
  include/engine/         # public API only (games may include these)
  src/                    # .cpp + private headers (not on the game include path)
  tools/asset_guid/       # write missing .meta + GUIDs (dev only)
  tools/asset_codegen/    # read-only: asset_ids.h + cooked catalog
  tests/                  # GoogleTest sources (no game code)
  builtin_assets/         # default shader/mesh/material/font; committed .meta; well-known GUIDs
  external/               # git submodules (SDL3, glm, glad, spdlog, boost_di, nanovg, tinyxml2, SDL_mixer, googletest, tomlplusplus)
```

Engine CMake **owns** third-party targets (including nanovg). Include paths use `${CMAKE_CURRENT_SOURCE_DIR}/external/…`, not a relative `../external/` that only works when the engine is nested under one specific game.

### 3.2 Game repo

```
your-game/
  external/engine/        # git submodule, url = ../engine (local); GitHub URL later
  CMakeLists.txt
  include/game/ …
  src/ …
  assets/                 # raw files + sidecar .meta; copied next to the exe
```

```cmake
add_subdirectory(external/engine)
engine_add_game(your-game src/main.cpp …)
```

`engine_add_game` owns codegen (`asset_ids.h` + cooked `catalog.toml` under the build tree), links `engine`, and copies `assets/` + `assets/engine/` beside the exe. Missing `.meta` → configure/build FAIL; run `asset_guid` locally and commit the new `.meta`.

After clone: `git submodule update --init --recursive` (engine’s own `external/` submodules included). Game CMake does not build `engine_tests` unless `-DENGINE_BUILD_TESTS=ON`. `ENGINE_WITH_WINDOW` defaults **ON** when Wind is a subdirectory (still **OFF** in the engine repo so `engine_tests` stay headless).

### 3.3 Why a separate repo

Nesting `engine/` directly under one game and vendoring SDL next to it cannot be a submodule. A standalone repo makes every game a sibling that pins a commit of `engine`.

### 3.4 Public headers vs private implementation

Two include roots. CMake:

```cmake
target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

A game `target_link_libraries(… PRIVATE engine)` therefore sees **only** `include/`. It cannot `#include` a file that lives under `src/`. There is **no** `include/engine/detail/` on the public path — putting “please don’t use this” headers under `include/` is not a facade.

**Public (`include/engine/`):** `engine.h` (umbrella), `igame.h`, `log.h` (facade, not spdlog), ECS (`world.h`, `entity.h`, components games spawn), `Time`, `Events<T>` / `EventReader` / `EventWriter`, `Command` / `CommandBuffer`, `ICanvas` / `IGraphicFactory` / `IMesh` / `IShader` / `ITexture` / **`IMaterial`**, `UiCanvas` / `ViewModel` / `Bindable` / `ICommand`, `AssetsDb`, `IAudioSystem` / `Sound`, `IFatalError`, `builtin_ids.h`. Games include these (or the umbrella).

**Private (`src/…`, never installed, never PUBLIC):** OpenGL/glad types, NanoVG context, SDL window/GL bootstrap, mixer tracks, Loop internals, importers, XML/CSS parsers, cooked-catalog parser. `OpenGLCanvas::draw` executes commands here.

**Forbidden in game code (and not possible if CMake is followed):** `#include <glad/…>`, SDL render/mixer headers, spdlog, NanoVG, tinyxml2, any `src/` engine header, `gl*` / `MIX_*` / `nvg*` calls.

This repo **splits** public and private headers instead of shipping every `.h` next to the game include path. `IGame::window_size` uses `glm::ivec2` — glm is a **PUBLIC** link of `engine`.

---



## 4. System architecture

```
main
  → Engine<GameT>::init()     Boost.DI + SDL + window + GL + UI canvas + audio
  → Engine::run()
       → Loop
            on_start
            while running:
              World::flush_events()              // age Events<T> (start of frame)
              frameDt = clamp(realDt, 0, 0.25)
              PollEvents (QUIT, resize, InputSystem → queues)
              ctx<WindowSize>, WindowResizeEvent if size changed
              ctx<MouseConsumed> = false
              IAudioSystem::update(frameDt)     // fades use wall-clock (also while paused)
              if not ApplicationState.paused:
                accumulator += frameDt
                steps = 0
                while accumulator >= kFixed && steps < kMaxFixedSteps:
                  Time.fixed_delta_time = kFixed
                  IGame::on_fixed_update()        // World::run(Schedule::Fixed)
                  accumulator -= kFixed
                  steps += 1
              Time.delta_time = frameDt
              IGame::on_update()                 // World::run(Schedule::Frame)
              ICanvas::draw()
            on_quit
```



### 4.1 Logical components


| Area      | Path                       | Responsibility                                     |
| --------- | -------------------------- | -------------------------------------------------- |
| Host      | `core/engine.h`            | DI graph, SDL init, window title/size from `IGame` |
| Time      | `core/time.h`              | `delta_time` (frame), `fixed_delta_time`, accumulator |
| Loop      | `core/loop.h`              | fixed-step sim + one frame pass + present          |
| Window    | `render/opengl/window_system.h`, `render/opengl/window_manager.h` | SDL window(s) + GL context(s); style/click-through (§21) |
| Input     | `core/input_system.h`      | scancode → `InputEvent`; mouse → `MouseEvent`      |
| Events    | `core/events.h`            | Bevy-style double-buffered `Events<T>`             |
| App state | `core/application_state.h` | `running`, `paused`, `Quit()`                      |
| Fatal     | `core/fatal_error.h`       | Dialog+quit in game; assertion in tests            |
| Log       | `core/log.h`               | Public facade; spdlog behind it                    |
| ECS       | `ecs/`                     | World, generational entities, views, engine systems |
| Render    | `render/`                  | Commands, camera, materials, sort, OpenGL backend  |
| UI        | `ui/`                      | XML + CSS + MVVM; `UiCanvas` draws into a Rect     |
| Resources | `resources/`               | AssetsDb, cooked catalog, TOML `.meta` (§10)       |
| Audio     | `audio/`                   | `IAudioSystem` (see §11)                           |
| Haptics   | `haptics/`                 | `IHaptics` (see §18)                               |
| Codegen   | `tools/asset_guid`, `asset_codegen` | Write GUIDs vs read-only generate            |
| Tests     | `tests/`                   | GoogleTest (`engine_tests`, see §12)               |




### 4.2 Dependency injection and systems

`Engine<GameT>::init` builds a Boost.DI injector. Singletons include `ApplicationState`, `Time`, `AssetsDb`, `InputSystem`, `IFatalError`, `CommandBuffer`, `ICanvas` → `OpenGLCanvas`, `IGraphicFactory` → `OpenGLFactory`, `IRenderBackend` → `OpenGLRenderBackend`, `IAudioSystem` → `AudioSystem`, `IGame` → `GameT`.

`IGame` is constructed by the injector (constructor parameters = services). Systems are **not** resolved from DI inside `on_update()`. They capture `shared_ptr` services when constructed in `on_start`, or they read `World::ctx<T>()` (`Time`, `WindowSize`, `MouseConsumed`, `ActiveCamera`, `Events<U>`).

`engine::register_engine_systems(world, …)` is called by the **host** after `World` exists and **before** `on_start`. Games only `add_system` into **`Phase::Game`**.

Do not introduce a service locator (`Engine::get_audio()`).

Games receive services through `Game`’s constructor. Systems write/read typed events (`EventWriter` / `EventReader`); they must not load files or pass paths.

### 4.3 One world, no Node graph

There is no `Node` / `NodeEcs` / `NodeUI` scene-graph type beside ECS.

`IGame` owns `ecs::World`. `on_start` registers **game** systems onto `Schedule::Fixed` or `Schedule::Frame` at `Phase::Game`. `on_fixed_update` / `on_update` run those schedules (see §4.4–§4.5).

```
ecs::World
  entity Camera     + Camera (ortho, auto_aspect) + optional Transform
  entity HUD        + UiCanvas { document, stylesheet, data_context, fit = FillWindow }
  entity PauseMenu  + UiCanvas { …, order = 1 }   // optional
  entity Player     + Transform + Renderable + …
```

Widget buttons are **not** ECS entities. The visual tree is the **instance** of an XML document under `UiCanvas` (WPF visual tree vs view-model). Layout (flex, gap) lives in markup + CSS, not in the registry.

World-space labels later: same `UiCanvas`, `rect` written each frame from `Transform` + `Camera::WorldToScreen` (`fit = Fixed`). No second graph.

Mouse: `UiInputSystem` (phase `Input`) hit-tests canvases **front-to-back** (`UiCanvas::order`, then entity index). Only the front canvas whose `rect` contains the pointer is considered; a miss on that canvas does not fall through for `ICommand`. `MouseConsumed` is true only on a **widget hit** (v1: `Button`); empty chrome / labels / Image do not consume. The bound `ICommand` runs if `can_execute()` (see §8.5). Gameplay click systems in `Phase::Game` must respect `MouseConsumed`.

There is **no** `Transform` parent. A turret that must follow a tank is a game concern in v1 (copy position in a system) until a `Parent` component exists.

### 4.4 Fixed timestep

An earlier draft of this SDD passed a **clamped variable `dt`** into every system. That makes physics and turn timing frame-rate dependent. This repo does **not** do that.

Constants (in `Time` / Loop):

| Name | Value | Role |
| --- | --- | --- |
| `kFixed` | `1/60` s | one simulation tick |
| `kMaxFixedSteps` | `8` | spiral-of-death cap (hitch → at most 8 ticks, then drop remainder) |
| `frameDt` clamp | `0.25` s | ignore a huge stall as one giant frame |

`Time` fields:

- `delta_time` — this **frame’s** clamped wall time (UI animation, audio fades already ticked with `frameDt` in Loop).
- `fixed_delta_time` — always `kFixed` inside `on_fixed_update`.
- `alpha` — `accumulator / kFixed` after the sim loop (0..1). Reserved for interpolating renderables later; v1 may ignore it.

**Which schedule:**

| Schedule | When | Put here |
| --- | --- | --- |
| `Fixed` | 0..N times per frame, dt = `kFixed`; **skipped while paused** | physics integrate, collision probe, movement, anything that must be fps-independent |
| `Frame` | once per frame, dt = `delta_time`; **always runs** (pause menus, UI) | input, bindings, render, UI, click-to-cell / other **one-shot input** gameplay |

Input is polled **once per frame** before the `while`. A click is visible to Frame systems **this same frame** (§9). If a click system ran on **Fixed**, two sim steps in one frame could apply the same click twice. **One-shot input gameplay runs on Frame, `Phase::Game`.** Held keys (state map) are fine to read from Fixed.

`IGame::on_fixed_update` → `world.run(Schedule::Fixed)`. `IGame::on_update` → `world.run(Schedule::Frame)`. Do not call `world.run` for both schedules from a single hook.

Tests: given `accumulator` math (or a testable `FixedStepClock`), `dt = 1/60` → 1 step; `dt = 2/60` → 2 steps; `dt = 9 * kFixed` → exactly `kMaxFixedSteps` and leftover discarded. While `paused`, zero Fixed steps and accumulator does not grow.

### 4.5 Phases (order inside a schedule)

Registration order inside a **phase** is execution order. Games do not pick a raw index among engine systems. They only add `Phase::Game`.

**`Schedule::Fixed`**

| Phase | Who | Does |
| --- | --- | --- |
| `Physics` | engine | integrate, AABB probe, `CollisionEvent` |
| `Game` | game | movement responses, gameplay that must be fps-independent |

**`Schedule::Frame`**

| Phase | Who | Does |
| --- | --- | --- |
| `Input` | engine | `UiInputSystem` (hit-test, `ICommand::execute`, `MouseConsumed`) |
| `Game` | game | world picking if not consumed; mutate ViewModels; `EventWriter` |
| `Bind` | engine | push `Bindable<T>` / commands into the XML instance tree |
| `Audio` | engine | `EventReader<PlaySfxEvent>` / music — **after** Game so same-frame SFX work |
| `Render` | engine | sort `Renderable`s, push `CmdDrawMesh` |
| `UiRender` | engine | push `CmdDrawUI` (so HUD is on top of the world) |

`add_system(Schedule, Phase::Game, system)` is the game API. Engine phases are registered by `register_engine_systems`.

### 4.6 Pause

`ApplicationState::paused` (bool). Loop: **do not** run Fixed, **do not** add to `accumulator` (unpause must not dump 8 sim steps). Frame still runs so a pause `UiCanvas` can bind Continue/Quit.

Audio `update(frameDt)` still runs (music keeps fading unless the game `stop_music`). Gameplay SFX from skipped Fixed systems simply do not fire.

### 4.7 Window resize and camera

With a single window, `WindowSize` lived directly in `World::ctx`. Once a game can own more than one `WindowId` (§21), a lone `ctx<WindowSize>()` is ambiguous — resize now goes through a per-window map:

```cpp
struct WindowSizes {
    std::unordered_map<WindowId, glm::ivec2> sizes;   // drawable size in pixels (SDL), per window
};
```

On `SDL_EVENT_WINDOW_RESIZED` / `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` (and at `on_start`, once per window that exists): write `ctx<WindowSizes>().sizes[id]`, `EventWriter<WindowResizeEvent>{ id, w, h }`. A single-window game still reads `ctx<WindowSizes>().sizes[kPrimaryWindow]` — there is no separate "simple" path for one window.

- `UiCanvas::window` (default `kPrimaryWindow`) says which window's size drives its `rect`.
- `UiCanvas::fit = FillWindow` → engine sets `rect = {0,0,w,h}` from that window's size before `Input`.
- `UiCanvas::fit = Fixed` → game owns `rect` (centered pause panel, world-space HUD).
- `UiCanvas::fit = ScaleWithScreenSize` → engine writes `rect` to the letterboxed, aspect-preserving real-pixel box for `reference_size` before `Input` (same timing as `FillWindow`); layout/paint/hit-test then run in `reference_size` design units through that box (§8.1).
- `Camera::auto_aspect = true` (default on the active camera) → rebuild ortho from `kPrimaryWindow`'s size; `screen_to_world` / `WorldToScreen` use that camera + `WindowSizes`. World rendering (`Renderable`) is a `kPrimaryWindow`-only concept in v1 — secondary windows carry UI-only canvases (§21.1), so there is no per-window camera yet.
- Active camera: `ctx<ActiveCamera>() = Entity`. Exactly one; missing camera is fatal on first `Render`.
- `ctx<WindowSizes>` is written **before** `on_start` for every window `IGame` declares up front, so `FillWindow` canvases spawned there get a real rect.

Default clear color remains black (opaque) unless the window's `WindowStyle::transparent` is set, in which case the primary window's canvas clears to `(0,0,0,0)` instead — see §21.2 — until a later `Camera::clear` field lets a game choose per-camera.

---



## 5. Game contract (`IGame`)

```cpp
class IGame {
public:
    virtual ~IGame() = default;
    virtual WindowDesc primary_window() const { return {}; }   // title "Game", size {800,600}, no style
    virtual ecs::World& world() = 0;
    virtual void on_start() = 0;
    virtual void on_fixed_update() = 0;  // Schedule::Fixed, 0..N times
    virtual void on_update() = 0;       // Schedule::Frame, once
    virtual void on_draw() = 0;
    virtual void on_quit() = 0;
};
```

`primary_window()` replaces the older separate `window_title()` / `window_size()` pair — this is a breaking change made while the engine is pre-1.0 (§21), not a compatibility shim; games update one override instead of two. Title/size/style are **not** hardcoded inside `Engine::init`. The host constructs `IGame` from the injector, then `WindowManager::create(kPrimaryWindow, game->primary_window())`. `World` exists after `Game` construction. Host calls `register_engine_systems` then `on_start` (scene spawn, `add_system` Game phase). Any window beyond the primary one is opened later, at the game's own request, through `Host`/`EngineRuntime` (§21.5) — `IGame` only declares the one window that must exist before `on_start` runs.

`on_draw` stays empty: world draw is `Phase::Render`; UI is `Phase::UiRender`; present is `OpenGLCanvas::draw`. Do not push commands from `on_draw`.

---



## 6. Rendering

A `Renderable` of raw `{ mesh, shader, texture }` has **undefined draw order** and one blend mode hardcoded for every sprite. This engine uses **materials** and an explicit **sort key** instead.

### 6.1 Command buffer

Commands (`std::variant`):

- `CmdDrawMesh` — mesh + **material** + model/view/projection + instance `color`.
- `CmdDrawUI` — bound XML instance tree, clipped to `UiCanvas.rect`.

```cpp
struct CmdDrawMesh {
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    glm::mat4 model, view, projection;
    glm::vec4 color{1, 1, 1, 1};
};
```

**There is no `CmdCustomDraw`.** A `std::function<void()>` escape hatch is not part of the command variant. Extra draw paths = new **named** command types in the engine (public header + private execute).

`CommandBuffer` is a FIFO. **Sort happens in `RenderSystem` before push**, not inside execute. `UiRender` runs after `Render`, so HUD commands follow world commands. Clear the buffer at the start of `Phase::Render` so Fixed systems never accumulate draws.

`OpenGLCanvas::draw`: clear → `CommandBuffer::execute` → `SDL_GL_SwapWindow`. Execute lives in `src/`.

### 6.2 Materials

A material is a cooked asset (`importer = "material"`), not three loose pointers on `Renderable`.

```cpp
enum class BlendMode { Opaque, Alpha, Additive }; // Additive = (src alpha, one)

class IMaterial {
public:
    virtual ~IMaterial() = default;
    virtual std::shared_ptr<IShader> shader() const = 0;
    virtual std::shared_ptr<ITexture> texture(int slot) const = 0; // 0 = albedo
    virtual glm::vec4 color() const = 0;
    virtual BlendMode blend() const = 0;
};
```

`.mat` sidecar (TOML) after the usual `guid` / `importer`:

```toml
guid = "…"
importer = "material"
shader = "32-hex-guid-of-shader"
blend = "alpha"              # opaque | alpha | additive
color = [1.0, 1.0, 1.0, 1.0]
[textures]
albedo = "32-hex-guid-of-texture"
```

`AssetsDb::get<IMaterial>(id)`. Games may multiply instance color on `Renderable`; they do not set GL blend in C++.

Shared material = one GPU bind if consecutive sorted draws share `IMaterial*`. No material-instancing graph in v1 (no Unity MaterialPropertyBlock beyond `Renderable::color`).

Engine **builtin** unlit sprite material + unit quad + default shader: well-known ids in `engine::builtin` (§10.8). Games that only need a tinted sprite use those plus their own albedo (or a `.mat` that already references it).

### 6.3 `Renderable` and sorting

```cpp
struct Renderable {
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    glm::vec4 color{1, 1, 1, 1};  // multiply with material color
    int layer = 0;                // coarse; world 0, foreground 10, …
    int order_in_layer = 0;       // painter order inside the layer
};
```

`RenderSystem` (`Phase::Render`) collects `view<Renderable, Transform>()`, sorts, then pushes `CmdDrawMesh`.

**Sort key (ascending), stable:**

1. `layer`
2. `order_in_layer`
3. material identity (pointer / `AssetId`) — batch when 1–2 tie
4. `Entity` index — deterministic when everything else ties

There is **no** automatic Y-sort or opaque-before-alpha pass that reorders across `order_in_layer`. Authors control painter’s algorithm with layer + order. `BlendMode` is applied at execute from the material; it does not change the sort.

Missing mesh or material on a `Renderable` → `IFatalError` (game bug), not a skipped draw that looks like a flicker.

### 6.4 Abstractions vs backend

| Interface | OpenGL impl |
| --- | --- |
| `IRenderBackend` | `OpenGLRenderBackend` |
| `ICanvas` | `OpenGLCanvas` |
| `IGraphicFactory` | `OpenGLFactory` |
| `IMesh` / `IShader` / `ITexture` / `IMaterial` | `OpenGL*` |

Game code depends on interfaces and `get<IMaterial>` / `get<ITexture>`, not glad or paths.

### 6.5 Camera and coordinates

Orthographic **Camera** component. `RenderSystem` uses `ctx<ActiveCamera>()`. `screen_to_world` / `WorldToScreen` take that camera + `WindowSize`.

| Space | Origin | Y |
| --- | --- | --- |
| SDL mouse / `UiCanvas.rect` | top-left of window | **down** |
| World / `Transform` | game-defined; default camera looks at origin | **up** |
| NanoVG inside a canvas | top-left of `rect` | **down** |

Default shader (builtin): GLSL 330, `uModel/uView/uProjection`, `uTexture`, `uColor`. Mesh format (`.mesh`): `pos.x pos.y pos.z uv.x uv.y` per vertex, `#` comments.

### 6.6 Blend (execute)

Set from `IMaterial::blend()` per `CmdDrawMesh`. UI is NanoVG in a later command (its own blend). Do not apply one global `(src alpha, one)` blend to every sprite.

---



## 7. ECS (EnTT as reference, not a dependency)

Do not vendor EnTT. Implement `ecs::World` **in this repo**, using EnTT as the **API and implementation reference** (generational index, sparse-set / packed storage, `view`, `try_get`, destroy that bumps generation).

Contract the homemade registry must keep (EnTT-like verbs, `snake_case` like the rest of the engine):

- `Entity` = index **plus generation**. Recycled ids do not alias live entities.
- `emplace` / `get` / `try_get` / `remove` / `destroy`.
- `view<T, U>()` — iteration over packed data, not `typeid().name()` string keys.
- Do not invalidate a view you are iterating; defer `destroy` if a system needs it (command buffer / `destroy` queue flushed after the view).
- `World::ctx<T>()` for singletons: `Time`, `WindowSize`, `MouseConsumed`, `ActiveCamera`, `Events<U>` (first access **registers** `U` for `flush_events`).
- Engine systems are registered by `register_engine_systems` into the phases in §4.5. They take `World&` plus constructor-injected `shared_ptr` services (`CommandBuffer`, `AssetsDb`, …).
- Game systems: `world.add_system(Schedule::Fixed | Frame, Phase::Game, …)` in `on_start` only.

Erase-from-vector pools that do not fix up indices, and raw `uint32_t` handles without a generation, are **not** used.

**Engine components:** `Transform` (no parent), `Renderable` (§6.3), `Camera`, `RigidBody`, `BoxCollider`, `UiCanvas` (§8).

**Physics:** AABB + velocity is a **collision probe**, not a solver. Writes `CollisionEvent` to `Events<CollisionEvent>` on enter. Bounce stays in game systems until a real solver exists.

---



## 8. UI (XML + CSS + MVVM)

NanoVG (GL3) draws the **instance** of a markup document. This is not a C++ `Layout`/`Label` tree with `onClick` lambdas, and not one ECS entity per widget.

WPF split, mapped to this engine:

| WPF | This engine |
| --- | --- |
| XAML | XML document asset (`importer = "ui"`) |
| ResourceDictionary / Style | custom CSS asset (`importer = "css"`) |
| `DataContext` + `{binding}` | `ViewModel` + `Bindable<T>` registered by name |
| `ICommand` / `RelayCommand` | `ICommand` / `RelayCommand` |
| code-behind `x:Class` | **none** — no `.cpp` for a view |
| `ControlTemplate` / VSM | **not v1** |

### 8.1 `UiCanvas`

```cpp
enum class UiFit { FillWindow, Fixed, ScaleWithScreenSize };

struct Rect { float x, y, w, h; };  // screen pixels, origin top-left (SDL)

struct UiCanvas {
    AssetId document;                       // .xml
    std::optional<AssetId> stylesheet;      // .css; else xml `stylesheet` attr
    std::vector<AssetId> extra_stylesheets; // after xml + stylesheet; later file wins at equal spec
    std::shared_ptr<ui::ViewModel> data_context;
    Rect rect{};                            // scissor + layout origin
    glm::vec2 reference_size{0.0f, 0.0f};   // design resolution; required when fit == ScaleWithScreenSize
    UiFit fit = UiFit::FillWindow;
    int order = 0;                          // higher = later draw / hit-test
};
```

`ScaleWithScreenSize` is the Unity `PanelSettings`-style "Scale With Screen Size" fit: layout, hit-testing, and painting all run in fixed `reference_size` design units (same XML/CSS as any other canvas — px means design px), and the engine derives one uniform `scale = min(window.w/reference_size.x, window.h/reference_size.y)` plus a centering `offset`, applied only at the paint/hit-test boundary (`ui::canvas_layout_space`). This keeps a pixel-art canvas laid out at its authored resolution (e.g. 576×696) and pixel-perfect at any window size, letterboxed rather than stretched. Use `FillWindow`/`Fixed` when the document's own CSS should react to the real window size instead (e.g. `%`-based responsive HUDs).

Spawn: `emplace<UiCanvas>(hud, { .document = assets::ui::hud, .data_context = hudVm })`. Game code does **not** `make_shared<Layout>()` or set `onClick`.

The runtime tree is owned by the UI module (cached instance per canvas). Reloading XML every frame is forbidden; rebuild when `document` / stylesheet / `DataContext` pointer changes.

Hit-test: mouse minus `rect` origin. Fonts: `get` + `importer = "font"`; CSS `font-family` names a font **AssetId** (hex) or a builtin name (`default`).

### 8.2 XML (markup)

Parsed with tinyxml2 **in `src/`**. Cooked catalog stores path + importer; runtime parses XML **once** on first `get<UiDocument>` (not every frame; not TOML).

v1 elements:

| Tag | Role |
| --- | --- |
| `Canvas` | root; optional `stylesheet="32-hex"` |
| `Stack` | flex-like box: `direction` horizontal/vertical, `gap`, `align` |
| `Label` | text |
| `Button` | hit-target; `command` binding |
| `Image` | `source` = texture/ui_image AssetId or `{binding}` |
| `ItemsControl` | repeats `ItemTemplate` over `items_source` |

```xml
<Canvas stylesheet="b0a1c2d3e4f5678901234567890abcde">
  <Stack class="hud" direction="vertical">
    <Label class="title" text="{binding title}"/>
    <Label text="{binding score}"/>
    <Button command="{binding restart}" content="{binding restart_label}"/>
    <ItemsControl items_source="{binding cells}">
      <ItemTemplate>
        <Button class="cell" command="{binding click}" content="{binding mark}"/>
      </ItemTemplate>
    </ItemsControl>
  </Stack>
</Canvas>
```

WPF-shaped `{binding path}` (path = registered snake_case name). `mode=one_way` default (VM → view). `mode=two_way` reserved (sliders); not required in v1.

`id` / `class` / `name` attributes: CSS hooks. `name` is not FindName-from-game; games do not reach into the tree.

Unknown tags / empty `{binding}` / intern hash collision of two paths: **`asset_codegen` fails the build**, and load-time is still fatal on `get`. Binding identifiers are `BindingId` via `constexpr intern(path)`. Codegen emits `assets::ui::Hud::bind(vm)` on a binder struct (not a generated ViewModel class). Handwritten `intern("x")` remains valid for tests and extra properties.

**Forbidden in XML:** filenames, `onClick`, inline GL, script. Asset refs are 32-hex GUIDs (or bindings that yield `AssetId`).

Building the same tree in C++ is allowed **only in `engine_tests`**.

### 8.3 Custom CSS

Not browser CSS. Engine parser in `src/` (no libcss). File + `.meta` `importer = "css"`.

**Selectors (v1):** `Element`, `.class`, `#id`, `Element.class`, descendant `A B`, child `A > B`. **No** adjacent/general sibling (`+`, `~`), no `,` grouping beyond listing duplicate rules — either warns and drops the rule.

**Pseudos:** `:hover`, `:pressed`, `:disabled` (buttons). No `:nth-child`, no `::before`.

**Units:** unitless number = **pixels**. `%` (of the parent content box) and `em` (of the element's `font-size`, default 16) are also supported. `calc()` accepts `+ - * /` with normal precedence and parens over px/%/em operands, on the length properties below; invalid `calc()` warns and drops the declaration. No `vw`, `vh`, `var()` (custom properties are not implemented at all — `var()` warns and drops the declaration).

**Properties (v1)** — ignore unknown with a **warn** (do not fail the whole sheet):

`color`, `background`, `opacity`, `visibility`, `width`, `height`, `min-width`, `min-height`, `padding` (1–4), `margin` (1–4), `gap`, `flex-direction`, `align-items`, `justify-content`, `border-radius`, `border-width`, `border-color`, `font-size`, `font-family`, `animation-name`, `animation-duration`, `z-index`, `position`, `top`, `right`, `bottom`, `left`, `transform`.

```css
.hud { padding: 16; gap: 8; flex-direction: vertical; }
.title { font-size: 24; color: #ffffff; }
Button { padding: 8 12; border-radius: 4; }
Button:hover { background: #333333; }
Button:pressed { background: #111111; }
Button:disabled { opacity: 0.5; }
.badge { position: absolute; top: 4; right: 4; z-index: 1; transform: rotate(15) scale(1.2); }
```

**Stacking, positioning, transform (v1):** `z-index` is a plain integer, **sibling-local** — direct children are stable-sorted by it (low = behind, matching `UiCanvas::order`'s "low first = behind" convention), tie-broken by document order; this is not full CSS stacking-context semantics. `position: static | relative | absolute`; `relative` offsets an element's own painted rect via `top`/`left` (or `-bottom`/`-right`) without reflowing siblings; `absolute` removes it from flow and resolves `top`/`right`/`bottom`/`left` against the nearest ancestor with `position` other than `static`, falling back to the canvas root — explicit or hug size by default, stretching to fill when both opposite insets are set with no explicit size on that axis. `transform: rotate(<deg>) scale(<factor>)` — rotation and uniform scale about the element's own center only, not a general matrix; layout itself is never transformed, only paint (and hit-testing, via an axis-aligned bounding-box approximation of the rotated/scaled corners — not a precise oriented-rect test). None of `z-index`/`position`/`top`/`right`/`bottom`/`left`/`transform` are MVVM-bindable or `@keyframes`-animatable in v1 (see below — only `opacity` is).

Cascade: element < class < id < pseudo. Later file rules win at equal specificity (rule index). The xml `stylesheet` attr is one GUID; extra sheets are `UiCanvas::extra_stylesheets` (concatenated after the xml sheet and optional `UiCanvas::stylesheet`).

**`@media`:** one feature query per block, `(min-width: N)` or `(min-height: N)` in px, matched against the window size at paint time. No nesting, no `and`/`or`, no other features — an unrecognized or nested query warns and drops the whole block.

**Animations:** `@keyframes <name> { from|to|N% { … } }`; an element opts in with `animation-name` + `animation-duration` (seconds). Only `opacity` is interpolated between the two bracketing keyframe stops, driven by `Frame::delta_time`. No `transition:`, no other animatable property.

`@import`, `var()`: **not v1**.

### 8.4 MVVM (C++, no reflection)

There is no C++ RTTI binding to arbitrary members. A `ViewModel` **registers** `BindingId`s (`constexpr ui::intern(path)`). Strings are intern input only — not map keys. Games call generated `assets::ui::Hud::bind(*this)` so XML paths name `vm.title` / `vm.restart` and missing members fail at compile. Do not generate `ViewModel` classes or `Bindable<T>` fields from XML (no types in markup).

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual bool can_execute() const = 0;
    virtual void execute() = 0;
};

template<typename T>
class Bindable { /* set/get; notifies the binding engine only */ };

class RelayCommand : public ICommand { /* ctor from std::function; can_execute bindable */ };

class ViewModel {
public:
    template<typename T>
    void property(BindingId id, Bindable<T>&);
    void command(BindingId id, ICommand&);
    // items_source: property(intern("cells"), cells) with BindableList<T>
};
```

Game:

```cpp
class HudViewModel : public engine::ui::ViewModel {
public:
    Bindable<std::string> title;
    Bindable<int> score;
    RelayCommand restart;

    HudViewModel() {
        assets::ui::Hud::bind(*this);
        restart = [this] { /* send event or mutate game model — not GL, not UI tree */ };
    }
};
```

**DataContext** on `UiCanvas` is inherited by children (WPF). `ItemsControl` sets the item as DataContext for each cloned `ItemTemplate`. Nested VMs are `Bindable<std::shared_ptr<ViewModel>>` if needed.

**Phase `Bind`:** copy registered values into the instance tree (text, content, `can_execute` → `:disabled`). One-way, every frame is acceptable in v1 (no dirty-rect requirement). `Bindable::set` from Fixed is visible next Frame Bind.

**Commands:** `UiInputSystem` on hit calls `ICommand::execute()` if `can_execute()`. That is the **only** UI → game path. `execute` may `EventWriter::send` or set other `Bindable`s. It must not include glad, touch `UIElement*`, or call `CommandBuffer`.

A `std::function<void()> onClick` on a widget is **not** part of the public API.

`INotifyPropertyChanged` is not a game-facing interface; `Bindable<T>` is the notification.

### 8.5 Input vs world

`MouseConsumed` is set **false** at the start of the Loop body, then `UiInputSystem` may set true. World picking in `Phase::Game` reads it. Do not reset it at end of frame.

`UiInputSystem` hit-tests canvases **front-to-back** (`UiCanvas::order`, then entity index). Only the front canvas whose `rect` contains the pointer is considered; a miss on that canvas does not fall through to a lower canvas for `ICommand`.

`MouseConsumed` is true only on a **widget hit**. v1 hit-target is `Button` (including when `can_execute` is false). Labels, empty Stack, Image without a command, and empty canvas chrome (FillWindow or Fixed) do not consume. Canvas-rect containment alone does not consume.

`InputSystem` does not filter `InputEvent` on `MouseConsumed` (UI has not run at poll). Gameplay in `Phase::Game` still reads the flag before treating mouse-bound Fire / world picks as a world action.

---



## 9. Input and events (Bevy queues)

An immediate-callback `EventBus` (`Subscribe` + `Emit`, no unsubscribe) is **not used**: immediate observer lists dangle when a node dies and re-enter unsafely during `Emit`. UI clicks are **not** a second bus: they are `ICommand` (§8.4).

Replace with **double-buffered queues**, same shape as Bevy `Events<T>` / `EventReader` / `EventWriter`.

**Storage:** `Events<T>` lives in `World::ctx<Events<T>>()`. First access **registers** `T` on a type-erased list. `World::flush_events()` (start of Loop) calls `update()` on every registered type. There is **no** central `EventQueues` object that must know game event types at compile time of the engine.

**Lifetime (Bevy-like, two frames):**

- `flush_events` at **start** of the iteration ages buffers (drop events older than one extra frame).
- `EventWriter<T>::send` appends to the **current** buffer.
- `EventReader<T>` iterates **previous + current** (events sent earlier this frame are visible to later phases). Input polled after Flush is therefore visible to `Phase::Input` **the same frame**. `PlaySfxEvent` sent in `Phase::Game` is visible to `Phase::Audio` the same frame.
- Events do not live forever.
- No `Unsubscribe`: readers hold no allocation in the queue; writers do not store `this`.

Input:

- `InputSystem` (SDL poll in Loop, before schedules) writes `InputEvent` / `MouseEvent` / held-state map. It does not call UI.
- Bindings: `Control` → `ActionId`. Intern names via `InputSystem::intern`; gameplay uses `ActionId`, not action strings. `KeyCode` names match SDL3 scancodes (no SDL in public headers). Mouse buttons may bind to `ActionId` (`bind(MouseButton, …)`); `handle_mouse_button` always emits `MouseEvent` (UI / pick) and, if that button is bound, the same down/up / held / `InputEvent` path as keys. `InputSystem` does not filter `InputEvent` on `MouseConsumed` (UI has not run at poll). Gameplay in `Phase::Game` must respect `world.ctx<ui::MouseConsumed>().value` before treating mouse-bound Fire as a world action. Mouse move is not an action.
- Key **down / up / held** must be represented (held = state map updated from down/up, not a one-shot event only).

`play_sfx` from gameplay: `EventWriter<PlaySfxEvent>` (preferred) so `Phase::Audio` plays it. Direct `IAudioSystem` from a game system is allowed. Still no filenames.

---



## 10. Assets

Loading by filename (`get<ITexture>("ball.png")`) breaks when files move and puts import policy in C++. This engine loads **only by GUID**. The raw bytes + a sidecar `.meta` are the source of truth; C++ sees generated constants.

### 10.1 `AssetId`

Strong type wrapping a **32-character lowercase hex** GUID (same length/charset as Unity `.meta`). Invalid length/charset is a load error. GUIDs are unique in a game’s `assets/` tree and **never change** after the asset is referenced from code or other metas.

```cpp
db.get<ITexture>(assets::textures::player);      // T; missing cooked → fatal
db.try_get<Sound>(assets::sfx::step);             // Result; caller handles NotFound
```

Forbidden: `get<T>("player.png")`, concatenating `ASSETS_PATH` in game code, dereferencing a null `get`.

### 10.2 Authoring layout

```
assets/
  textures/player.png
  textures/player.png.meta
  materials/player.mat
  materials/player.mat.meta
  ui/hud.xml
  ui/hud.xml.meta
  ui/hud.css
  ui/hud.css.meta
  sfx/step.wav
  sfx/step.wav.meta
```

Every raw file (not `.meta`, not generated) **must** have a sidecar `filename.ext.meta` **in git**. `asset_codegen` (build) **never writes** GUID files. Missing sidecar, bad GUID, or collision → **build fails**.

To add a new file: drop the raw asset, run **`asset_guid`** (separate executable) once; it creates default TOML `.meta` with a new GUID. Commit the `.meta`. Then CMake `asset_codegen` can emit `asset_ids.h`.

`.meta` is **TOML** (tomlplusplus), not YAML.

Rename/move the raw file together with its `.meta`. The GUID stays; `asset_codegen` only changes the C++ identifier derived from the new path.

### 10.3 Importer schemas

Every `.meta` starts with:

```toml
guid = "a1b2c3d4e5f6789012345678901234ab"
importer = "texture"   # texture | audio | mesh | shader | font | ui_image | material | ui | css
```

**`importer = "texture"`** (PNG → `ITexture` / sprite):

```toml
guid = "…"
importer = "texture"
color_space = "srgb"       # srgb | linear
filter = "linear"          # nearest | linear
wrap = "clamp"             # clamp | repeat | mirror
layout = "single"          # single | multiple (atlas)

# layout = "multiple"
# [[sprites]]
# name = "idle"
# rect = { x = 0, y = 0, w = 16, h = 16 }
```

**`importer = "audio"`** (WAV → `Sound`: clip + bank):

```toml
guid = "…"
importer = "audio"
bank = "sfx"               # sfx | music
volume = 1.0
pitch_range = [1.0, 1.0]
loop = false
```

`volume` / `pitch_range` **are** the sound bank. Games do not keep a parallel `GameSounds` struct with the same numbers.

**`importer = "material"`:** the `.mat` file is TOML (`shader`, `blend`, `color`, `[textures]`, §6.2). `.meta` is only `guid` + `importer` (same split as `.shader` XML vs sidecar). Codegen fails if `shader` / albedo GUIDs are missing or unknown.

**`importer = "ui"`:** `.xml` markup (§8.2). **`importer = "css"`:** `.css` sheet (§8.3).

**`importer` mesh / shader / font / ui_image:** GUID + importer-specific defaults (font size is draw-time / CSS; shader/mesh path only in v1).

Unknown keys: warn. Missing `guid` / `importer`: **codegen fails** (runtime never sees that file).

### 10.4 Tools: `asset_guid` vs `asset_codegen`

Two executables, different trust boundaries.

| Tool | When | Writes source tree? | Role |
|---|---|---|---|
| `asset_guid` | Developer, by hand | **Yes** — missing `.meta` only | New random GUID + default importer from extension |
| `asset_codegen` | CMake every game build | **No** | Parse TOML metas; fail on missing/invalid/collision; emit `asset_ids.h` + **cooked catalog** (guid, relative path, **parsed importer blob**) |

`asset_codegen` output: build-tree `generated/` (`engine_add_game`). Gitignore generated files. **Commit `.meta`.** CI must not run `asset_guid`.

Constants still:

```cpp
namespace assets {
namespace sfx {
inline constexpr engine::AssetId step{"a1b2c3d4e5f6789012345678901234ab"};
}
}
```

### 10.5 Runtime `AssetsDb`

On init, load the **cooked catalog** (guid → path + importer settings). Runtime **does not parse TOML** in the player. Authoring `.meta` is for `asset_guid` / `asset_codegen` only.

`try_get` / `get` use the catalog, then load raw bytes from `ASSETS_PATH + relativePath` with the cooked importer fields. Cache by `(AssetId, T)`.

`ASSETS_PATH` is `<exe dir>/assets` (base path of the executable), not `cwd`.

| T | Importer | Raw |
|---|---|---|
| `ITexture` | `texture` | `.png` |
| `UIImage` | `ui_image` | `.png` |
| `IMaterial` | `material` | `.meta` fields (§6.2) |
| `UiDocument` | `ui` | `.xml` |
| `StyleSheet` | `css` | `.css` |
| `Sound` | `audio` | `.wav` |
| `IMesh` | `mesh` | `.mesh` |
| `IShader` | `shader` | `.shader` |
| font handle | `font` | `.ttf` |

Decoded `MIX_Audio` is an implementation detail of the audio importer; game code asks for `Sound`.

### 10.6 Sprite sheets (`layout = "multiple"`)

v1 may ship `layout = "single"` only. The TOML schema includes `[[sprites]]` so re-packing an atlas later does not change GUIDs. `get<ITexture>` returns the atlas; sprite rects from cooked meta (`get_sprite(id, "idle")` when implemented).

### 10.7 `get` vs `try_get`

`Result` with only `{ T, None }` hides **corrupt vs missing**. Use an error enum.

```cpp
enum class AssetError {
    NotFound,
    Corrupt,
    TypeMismatch,
    NotReady        // Get before GL/audio init
};

std::expected<std::shared_ptr<T>, AssetError> try_get(AssetId);

std::shared_ptr<T> Get(AssetId);  // never null
```

- **`try_get`:** caller must handle `AssetError`. `NotFound` is valid for optional content. `Corrupt` / `TypeMismatch` should usually still be treated as fatal by the game, but the API does not hide them as `None`.
- **`get`:** `try_get` + on any error call `IFatalError` (message includes GUID + error) and **do not return**. Game hook: system dialog (e.g. `SDL_ShowSimpleMessageBox`) + `ApplicationState::Quit()` / abort so the process does not continue with a missing cooked asset. Test hook: `ADD_FAILURE` / fail the test — **no dialog**.

`get` is the default in gameplay. `try_get` is for content that may be absent (mod slot, optional pack). Examples in this SDD must not dereference a pointer that can be null.

`IFatalError` is injected; `AssetsDb` does not hardcode Win32/`MessageBox`.

### 10.8 Engine builtin assets

Default shader / unit quad / unlit sprite material / UI font are **not** copied by hand into every game.

- Source: `engine/builtin_assets/` with committed `.meta` and **stable well-known GUIDs**.
- CMake copies them to `<exe dir>/assets/engine/` (engine tests and games).
- Runtime loads a **second** cooked catalog from that folder (engine-owned, not emitted by the game’s `asset_codegen`). Game codegen scans **only** the game `assets/` tree.
- Codegen is given the builtin GUID list and **fails** if a game `.meta` reuses one.
- Public constants: `include/engine/builtin_ids.h` (`engine::builtin::shader_unlit`, `mesh_quad`, `material_unlit`, `font_ui`). **Do not regenerate these GUIDs.**

Games still `get<IMaterial>(engine::builtin::material_unlit)` (or a game `.mat` that references a game texture + builtin shader GUID).

---



## 11. Audio



### 11.1 Design rationale

A filename-keyed `PlaySoundEvent{name}` → `MIX_PlayAudio` with no gain/pitch/stop control, and music as a single `MIX_Track`, does not scale to a game with UI clicks, overlapping SFX, or music transitions.

Target model follows Lumenwake `IAudioSystem` / `SoundData` / SFX pool / dual music sources / looping handles, mapped onto SDL3_mixer:


| Lumenwake              | This engine                           |
| ---------------------- | ------------------------------------- |
| `AudioClip`            | decoded `MIX_Audio` inside the audio importer |
| `SoundData`            | `Sound` from `importer: audio` `.meta`        |
| `AudioSource`          | `MIX_Track`                           |
| `AudioMixer` groups    | linear bus gains × `MIX_SetTrackGain` |
| DOTween fade           | lerp in `IAudioSystem::update(dt)`    |
| Zenject `IAudioSystem` | Boost.DI `IAudioSystem`               |




### 11.2 `Sound`

Produced by `AssetsDb::get<Sound>(AssetId)`, not hand-filled in game code.

```cpp
struct Sound {
    std::shared_ptr<Audio> clip;     // decoded WAV
    float volume = 1.f;              // from .meta
    glm::vec2 pitch_range {1.f, 1.f}; // from .meta
    bool loop = false;
    // bank: sfx | music — default bus when playing
};
```

`IAudioSystem` still takes `Sound` (so tests can build one without a catalog). Games resolve GUID → `Sound` via AssetsDb.

### 11.3 `IAudioSystem`

```cpp
class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    virtual bool init() = 0;
    virtual void dispose() = 0;
    virtual void update(float dt) = 0;   // fades, recycle SFX tracks

    virtual void play_sfx(const Sound& sound, float volume_scale = 1.f) = 0;

    virtual void play_music(const Sound& sound, bool loop = true, float fade_seconds = 0.f) = 0;
    virtual void stop_music(float fade_seconds = 0.f) = 0;
    virtual bool is_music_playing() const = 0;

    virtual LoopingSfxHandle create_looping_sfx() = 0;
    virtual void play_looping_sfx(LoopingSfxHandle, const Sound&, float fade_in = 0.f) = 0;
    virtual void stop_looping_sfx(LoopingSfxHandle, float fade_out = 0.f) = 0;
    virtual void release_looping_sfx(LoopingSfxHandle, float fade_out = 0.f) = 0;

    virtual void set_master_volume(float) = 0;  // 0..1
    virtual void set_music_volume(float) = 0;
    virtual void set_sfx_volume(float) = 0;
};
```

`LoopingSfxHandle` is an opaque id (`0` = invalid), same idea as Lumenwake’s struct.

### 11.4 Internals

- **SFX pool:** ~12 `MIX_Track`s. `play_sfx` acquires a free track (`!MIX_TrackPlaying`); if none, **skip** (no steal, no queue). Pitch via `MIX_SetTrackFrequencyRatio`. Gain: `master * sfxBus * sound.volume * volume_scale`.
- **Music A/B:** two tracks. `play_music` with `fade_seconds > 0` and something already playing crossfades (incoming gain 0→target, outgoing →0 then stop). Immediate play if fade is 0 or idle.
- **Looping registry:** handle → dedicated track; fade in/out in `Update`. Enough for looping engine/ambience sounds; a simple game may leave it unused.
- **Buses:** no Unity mixer. `final_gain = master * bus * voiceVolume`. Mute ≈ very small gain (SDL has no dB mixer).
- **Tick:** `Loop` calls `IAudioSystem::update(frameDt)` every **frame** (wall-clock fades), not once per fixed step.
- **Format:** WAV only (mixer flags: OGG off).



### 11.5 Event queues (SFX)

```cpp
struct PlaySfxEvent { engine::AssetId id; float volume_scale = 1.f; };
struct PlayMusicEvent { engine::AssetId id; bool loop = true; float fade_seconds = 0.f; };
```

An audio system in `Phase::Audio` (`EventReader<PlaySfxEvent>`): `get<Sound>(id)` (fatal if the cue is required) then `IAudioSystem`. No filename events.

### 11.6 Game usage

```cpp
audio->play_music(*db.get<Sound>(assets::music::theme));
audio->play_sfx(*db.get<Sound>(assets::sfx::step));
```

`get` returns `std::shared_ptr<T>` that is never null; the `*` is a reference to a live object, not a null check.

---



## 12. Testing (GoogleTest)

Tests live **in the engine**, not in each consuming game.

### 12.1 Targets and CMake

- Submodule: `external/googletest` (`https://github.com/google/googletest.git`).
- Binary: `engine_tests`, linked with `engine` + `GTest::gtest_main`.
- `enable_testing()`, `include(GoogleTest)`, `gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)` (same as Q+).
- MSVC: `gtest_force_shared_crt ON`; `INSTALL_GTEST OFF`; `BUILD_GMOCK OFF` until a test needs `NiceMock`.

`ENGINE_BUILD_TESTS` defaults to **ON** when this repo is the CMake root, **OFF** when a game does `add_subdirectory(external/engine)` — so games do not compile `engine_tests` unless they pass `-DENGINE_BUILD_TESTS=ON`. A game that wants GoogleTest for its own test binary without also compiling `engine_tests` sets `ENGINE_WITH_GTEST ON` instead (defaults to `ENGINE_BUILD_TESTS`'s value, so `ENGINE_BUILD_TESTS=ON` still implies it).

```bash
# from engine/
cmake -S . -B ./build
cmake --build build --target engine_tests
ctest --test-dir build --output-on-failure
```

A tiny dummy `IGame` (or tests that never call `Engine::run`) is enough to link the static lib. Do not boot a real window in default CI.

### 12.2 What to cover (required)

Prefer **pure logic** and fakes over GPU/mixer. Extract policy (gain, pool, AABB) so it is testable without `MIX_Track`.

| Area | Tests |
|---|---|
| ECS | generational Entity; try_get after destroy is empty; view<A,B>; deferred destroy during iteration |
| Event queues | send; reader sees current+previous; `flush_events` drops older than two frames; first `ctx<Events<T>>` registers T |
| CommandBuffer | push `CmdDrawMesh` (material, not raw shader) / `CmdDrawUI`; execute order; clear between frames; **no** custom-callback |
| Sort | layer, order_in_layer, material, entity; stable; UI commands after world |
| Materials | parse `.mat` TOML; missing shader GUID fails codegen; instance color multiplies |
| UiCanvas | FillWindow rect on resize; ScaleWithScreenSize letterboxed rect + design-space hit-test on resize; widget hit (Button) for MouseConsumed; order; MouseConsumed reset each frame |
| UI XML/CSS | parse subset; unknown element fatal; `{binding}` missing name fatal; CSS unknown prop warn |
| MVVM | property/command registration; OneWay bind updates label text; Button click calls ICommand; onClick API absent |
| Loop / Time | fixed-step accumulator; cap at `kMaxFixedSteps`; **paused** → 0 Fixed steps, accumulator frozen |
| Physics | integrate velocity with `fixed_delta_time`; AABB overlap; `CollisionEvent` on enter, not every stay frame |
| Camera | `screen_to_world` / orthographic bounds for a known window size |
| Input | binding scancode → action string; unbound key ignored (synthetic `SDL_Event` if possible, else a small mapper unit) |
| Audio policy | `final_gain = master * bus * voice`; clamp 0..1; SFX pool acquire/release; skip when pool exhausted; music A/B index swap on crossfade; invalid `LoopingSfxHandle` is a no-op |
| Assets / meta | parse texture + audio **TOML**; reject bad GUID; cooked catalog guid→path+importer; `try_get` NotFound vs TypeMismatch; `get` calls fatal hook; identifier from path; **codegen fails** if `.meta` missing; collision fails |

Test files: `tests/<area>_test.cpp` (`ecs_test.cpp`, `events_test.cpp`, `audio_test.cpp`, `assets_test.cpp`, …). One fixture per area is enough.

### 12.3 What not to cover in `engine_tests`

- Pixel-perfect OpenGL / NanoVG screenshots.
- `Engine<GameT>::init` + real SDL window (needs a display; optional local `ENGINE_MANUAL_GL_TEST`).
- Decoding a WAV through SDL_mixer in CI (no audio device). Use a fake `Audio` / fake track for pool tests.
- Gameplay (bot AI, score) — that belongs in the game repo, not here.

### 12.4 Fakes

Do not `#include` glad in tests. For audio, a `FakeTrack` / `FakeMixer` (or a pool templated on a `Track` concept) keeps §11 behavior under test without `MIX_Init`. Command execution can record calls instead of drawing.

A feature in §6 / §8 / §10 / §11 / ECS / events is **not done** until `engine_tests` has a case for the happy path and the main failure (empty pool, unknown GUID, missing `.meta` at codegen, unknown XML tag, missing binding name).

---

## 13. Coding conventions

- Namespaces: `engine`, `engine::ecs`, `engine::render`, `engine::ui`.
- Files: `snake_case` (`input_system.cpp`). **Public** headers under `include/engine/…`. **Private** headers live next to `.cpp` under `src/…` (not mirrored into `include/`). Tests: `tests/<area>_test.cpp`.
- Types / enums: `PascalCase`. Interfaces: `I` prefix (`IGame`, `IMaterial`).
- Functions and methods: `snake_case` (`try_get`, `on_start`, `play_sfx`). Constructors keep the type name.
- Fields: `snake_case` (`delta_time`, `order_in_layer`). Private members: `name_`.
- Constants: `kPascalCase` (`kFixed`, `kSfxPoolSize`).
- UI XML: tags match types (`Button`, `Label`). Attributes and `{binding}` paths are `snake_case` (`text`, `command`, `items_source`). CSS properties stay kebab-case. CSS element selectors match tags.
- Game aliases (optional): `e`, `er`, `ecs`, `eui`.
- `.clang-format`: LLVM-based, 4 spaces, column 120.
- Do not introduce a service locator. Do not call `MIX_*` / `gl*` / `nvg*` from game code. Do not include spdlog from game code.
- Do not add `CmdCustomDraw` or any `std::function` draw callback to the public command variant.
- Do not add `onClick` lambdas to UI widgets. UI → game is `ICommand` only.
- Do not put shader/texture on `Renderable`; use `IMaterial`.

---



## 14. Build

**Engine standalone** (library + tests):

```bash
git submodule update --init --recursive
cmake --preset vs
cmake --build build --target engine_tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Preset `vs` is Visual Studio 18 (this machine). `vs2022` exists if that generator is installed. Any C++23 capable generator is fine (Ninja + clang/msvc).

**Game** (engine as submodule; tests off by default):

```bash
git submodule update --init --recursive
cmake -G "Visual Studio 17 2022" -S . -B ./build
cmake --build build --target <game>
```

Runtime: `build/bin/<Config>/` with game `assets/` **and** `assets/engine/` (builtin, §10.8) copied beside the exe. Game build runs `asset_codegen` so `asset_ids.h` exists before compiling sources.


---



## 15. Key design decisions

1. Third-party libraries live in this repo's own `external/`; engine CMake adds those subdirectories.
2. `IGame::primary_window()` (originally separate `window_title` / `window_size`, folded into one `WindowDesc` — §21.2); `Engine` uses it instead of a hardcoded title/size.
3. `IAudioSystem` (§11) instead of raw mixer calls and a filename-keyed event manager.
4. `Loop` ticks `IAudioSystem::update`.
5. `IAudioSystem` is bound in DI instead of exposing `MIX_Mixer*` to games.
6. `external/googletest`, `external/tomlplusplus`, `tests/`, `engine_tests` (§12). No EnTT package.
7. GUID `AssetsDb`: TOML `.meta`, `asset_guid` + `asset_codegen`, `get` / `try_get` (§10).
8. Bevy-style `Events<T>` (§9), not an immediate-callback bus. Homemade ECS with an EnTT-like API (§7).
9. No `Node` / `NodeEcs` / `NodeUI`. UI = `UiCanvas` + XML document + ViewModel (§4.3, §8). No `onClick` lambdas.
10. Fixed timestep Loop + `IGame::on_fixed_update` + `Schedule` / `Phase` (§4.4–§4.5). No variable `dt` into physics.
11. No `CmdCustomDraw`. `CmdDrawMesh` carries `IMaterial`, not shader+texture (§6).
12. Public `include/engine/` vs private `src/` headers; glm PUBLIC, SDL/glad/spdlog/NanoVG not (§3.4).
13. Sort `Renderable` by layer / order_in_layer / material / entity (§6.3).
14. Builtin assets + well-known GUIDs (§10.8).
15. `Events<T>` in `World::ctx`; `flush_events` at start of frame (§9).
16. Pause skips Fixed and freezes accumulator; resize updates `FillWindow` canvases (§4.6–§4.7).
17. App icon splits into runtime (`IGame::window_icon`, `AssetId`-based) vs. packaging (host-tool-generated `.ico`/`.icns`/mipmap/favicon, CMake/Gradle-time only) — no shared abstraction (§19).
18. Splash screen is one `UiCanvas`/`Image` entity reusing the existing CSS `@keyframes` opacity animator, not a new draw path or a `Host::tick` pause — `SplashScreen` defaults to enabled with a builtin asset, but a game must call `ui::show_splash` explicitly to trigger it (no `IGame` auto-trigger) (§20).
19. Multi-window is in scope; multiple *games* sharing a process is still not (§2.2). One `ecs::World` stays authoritative — a second window is a second `WindowId` that a `UiCanvas` targets, not a second `World`/`Loop` (§21.1). Click-through is bounding-box hit-test, not framebuffer-alpha sampling, in v1 (§21.4).

---



## 16. Architectural rules (do not regress)

1. Game logic does not include glad / SDL render / mixer / NanoVG / spdlog / tinyxml2. Host includes `engine.h`. Game targets do not add `engine/src` to their include path (§3.4).
2. Draw only through `CommandBuffer`. The public variant has **no** custom GL callback. World draws use `IMaterial`, not loose shader/texture on the command.
3. Load only through `AssetsDb` by `AssetId`. Gameplay uses `get` (fatal). Optional content uses `try_get` and handles `AssetError`.
4. Play audio only through `IAudioSystem` with a `Sound` that came from the audio importer (or a test double).
5. Simulation / gameplay cross-talk: **event queues** (`send` / `read` / `flush_events`), not observer `Subscribe` on `this`. **UI → game:** `ICommand` on a `ViewModel` only — no `onClick` in game code.
6. Shared engine behavior ships with a GoogleTest, not only a game that “seems to work”.
7. Do not change an asset GUID after it is referenced. Move files with their `.meta`. Builtin GUIDs in `builtin_ids.h` are frozen.
8. `asset_codegen` never writes `.meta`. Missing sidecar is a **failed build**, not a random GUID in CI.
9. ECS is homemade, EnTT-shaped. **Do not add EnTT as a submodule.** Do not keep a Node graph beside World. No `Transform` parent in v1.
10. Simulation uses `fixed_delta_time` on `Schedule::Fixed`. One-shot clicks run on `Schedule::Frame`, `Phase::Game` (§4.4–§4.5).
11. UI markup is XML + CSS assets. Games do not build visual trees in C++ (tests excepted).
12. All engine APIs: **main thread only**.
13. `MouseConsumed` is cleared at the start of each Loop iteration, not at the end.
14. No engine code assumes a single global window. Rendering and input for a `UiCanvas` (and, in v1, all world `Renderable`s) go through its `WindowId`; a `ctx<T>()` singleton that used to mean "the window" is a `WindowId`-keyed map instead (`WindowSizes`, §4.7/§21).
15. Window/GL/SDL platform calls (`WS_EX_TRANSPARENT`, `SDL_SetWindowHitTest`, …) stay behind `WindowManager` in `src/render/opengl/`; `#if defined(_WIN32)` platform branches do not leak a Win32 type into `include/` (§21.4).

---

## 17. Open items (not blocking this SDD)

- Persist bus volumes (settings file) — game concern.
- Physics filename typo `physcis_system` — rename on extract.
- GitHub remote for this repo; games currently use relative submodule `../engine`.
- Optional later: gmock for `IRenderBackend`; game-repo tests for that game's own AI.
- `get_sprite(id, name)` for `layout: multiple` atlases (schema reserved in §10.6).
- Packed asset bundles (still GUID-addressed; catalog would point inside a pak).
- Separate `Sound` / cue asset that references a clip GUID (one WAV, several banks) — still one file = one cue until that exists.
- `Transform` parent / world-matrix chain.
- CSS `@import`, `var()`; WPF `ControlTemplate`, `VisualStateManager`, `IValueConverter`, `Mode=TwoWay`.
- `Renderable` `sort_mode = Y` (auto ground-sort) — not v1; use `order_in_layer`.
- Widget-as-ECS-entity (Bevy UI) — not v1; would replace the XML instance tree inside `UiCanvas`.
- Input: gamepad buttons/axes, touch, WASD composites, action maps, `MouseEvent` → `PointerEvent` rename — same `Control` / `ActionId` / `InputEvent` types, added as new `ControlKind`s (keyboard and mouse binds are done). Not a Unity Input System clone (no action callbacks).
- Adaptive Android launcher icon (foreground/background layers) — v1 icon ships as a flat legacy icon only (§19.3); adaptive needs a two-layer source input, not just the one master PNG `icon_codegen` takes today.
- Linux `.desktop` entry + icon-cache install — no `install()` target exists for games yet; out of scope until one does.
- macOS `.icns`/bundle path (§19.2) is untested in this repo's CI — no macOS machine or preset exists to build/run it on.
- Tying the splash screen (§20) to real asset-load completion instead of a fixed fade-in/hold/fade-out timer — not v1 (§20.4).
- Per-pixel (framebuffer-alpha) click-through — v1 is bounding-box hit-test only (§21.4).
- Transparent/borderless/always-on-top windows are validated on Windows only; Linux (compositor-dependent) and macOS are untested (§21.2, same status as the macOS icon path in §17 above).
- Per-window `Camera` / world rendering — v1 renders `Renderable`s only into `kPrimaryWindow`; secondary windows are UI-only (§21.1).
- Multi-monitor coordinate edge cases (DPI scaling differences between monitors, a window straddling two displays) — `set_window_position` takes virtual-desktop coordinates and does no clamping/validation in v1.
- GL-window transparency (§21.2) has never been visually verified against a real display, engine-side included — distinct from the existing "Windows-only, Linux/macOS untested" item above: even the validated Windows path has only been confirmed correct by reading source (`WindowSystem::create`'s `SDL_GL_ALPHA_SIZE` request, SDL's own `DwmEnableBlurBehindWindow` call), never by rendering an actual transparent window on an actual screen — this repo has no sample game and no GPU/display in CI or in the sandbox these changes were made in (§12.3). A downstream game (`td-over`) originally reported a transparent primary window rendering opaque black; investigating that report live (actually running the game, `GetWindowLongPtr` inspection, temporary diagnostic logging) found two real, unrelated bugs before transparency itself could even be exercised: the borderless-titlebar issue fixed in §21.2 above, and — the actual root cause of "nothing renders at all" — the downstream game's own `on_update()` override never called `world_.run(Schedule::Fixed/Frame)` (or `GameBase::on_update()`), so every engine-registered system (`Phase::Render`/`UiRender` included) silently never ran, for either window, regardless of transparency. With that fixed on the game side, transparency itself is still unconfirmed either way — still open.

---

## 18. Haptics

Device vibration: duration + intensity only (no waveform/pattern playback — out of scope for
v1). One frontend API — game code never sees which backend is active.

### 18.1 Design rationale

There is no vibration-relevant precedent to reuse from `IAudioSystem`'s Lumenwake mapping —
this is new ground. The interface follows the same shape anyway (pure-virtual `IHaptics` +
pimpl'd `HapticsSystem`, DI singleton, always-on fake state model) so it reads like the rest of
the engine, with two deliberate differences from Audio:

- No `ENGINE_WITH_*` build flag. Audio's `ENGINE_WITH_AUDIO` gates linking a third-party
  library (SDL3_mixer); haptics has no library to opt into, so the backend split is purely by
  platform — `#if defined(__EMSCRIPTEN__)` / `#elif defined(__ANDROID__)` / `#else` inside
  `HapticsSystem::Impl`, compiled unconditionally on every platform.
- No `update(float dt)`. Audio ticks every frame for wall-clock fades; vibration calls are
  fire-and-forget and timed by the OS/browser, so there is nothing to tick.

### 18.2 `IHaptics`

```cpp
class IHaptics {
public:
    virtual ~IHaptics() = default;

    virtual bool init() = 0;
    virtual void dispose() = 0;

    virtual void vibrate(float duration_seconds, float intensity = 1.f) = 0;
    virtual void cancel() = 0;

    virtual bool is_supported() const = 0;
};
```

Contract: `intensity` is clamped to `[0, 1]`. `duration_seconds <= 0`, or clamped
`intensity <= 0`, is a no-op — nothing is requested, and a vibration already running from an
earlier call keeps running (`vibrate()` never implicitly cancels; call `cancel()` explicitly).

### 18.3 Per-platform degradation

| Platform | Behavior |
| --- | --- |
| Native (desktop) | True no-op, no hardware. `is_supported()` is always `false`. |
| Web (Emscripten) | `navigator.vibrate(ms)` via an `EM_JS` shim — pure on/off, no amplitude control. `is_supported()` is a genuine runtime check (`typeof navigator.vibrate === 'function'`), not "compiled for Web ⇒ yes" (Firefox removed the API, Safari/iOS never shipped it). |
| Android API 26+ | Real amplitude control: `VibrationEffect.createOneShot(ms, amplitude)`, amplitude clamped to `[1, 255]` (never `0` — the API throws `IllegalArgumentException` on `0`, which is why the shared `intensity <= 0` no-op gate matters). |
| Android API 21–25 | Legacy `Vibrator.vibrate(long)`; amplitude ignored. |

Android reaches `android.os.Vibrator` via the first JNI code in this engine
(`SDL_GetAndroidJNIEnv()` / `SDL_GetAndroidActivity()` / `SDL_GetAndroidSDKVersion()`), resolved
once in `init()` and cached as global refs — `SDL_Haptic` was considered and rejected because it
only reaches external joystick/gamepad rumble motors, not the phone's own body vibrator.
`android.permission.VIBRATE` is a *normal* manifest permission (auto-granted at install, no
runtime prompt), declared in `cmake/android/app/src/main/AndroidManifest.xml`.

### 18.4 Testing

[[src.haptics.fake_haptics.h]] is an always-on state tracker in the same spirit as
`audio::FakeMixer`: real backend calls mirror onto it on every platform, so
[[tests.haptics_test.cpp]] exercises the full contract (gating, clamping, `cancel()`, fake-state
accessors) without a device or browser. The manifest permission is covered by a text-content
regression test (`AndroidManifestDeclaresVibratePermission` in
[[tests.cmake_sanity_test.cpp]]), the only feasible check since there is no real Android
manifest-merge build in this repo's CI.

---

## 19. Application icon

Two unrelated concerns share the name "app icon" and get two different mechanisms:

- **Runtime window icon** — shown in the title bar / taskbar while the game is running. Goes
  through `AssetsDb` like any other texture; per-game via `IGame`.
- **Packaging icon** — the `.exe` icon in Explorer, the macOS Dock/Finder icon, the Android
  launcher icon, the browser favicon. Exists before the engine runs at all; a CMake/build-time
  concern, not a runtime asset.

Do not unify these into one abstraction — they have different lifetimes (asset-catalog load
time vs. link/package time) and different platforms support only one of the two (§19.1).

### 19.1 Runtime window icon

```cpp
virtual std::optional<AssetId> window_icon() const { return std::nullopt; }
```

Added to `IGame` next to `window_title()` / `window_size()` (§5), default `nullopt` = OS
default icon. The source is a plain PNG in the game's `assets/` (importer `Texture` or
`UiImage` — no new importer). `WindowSystem::set_icon(const render::TextureDesc&)` builds an
`SDL_Surface` with `SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba.data(), w * 4)` and
calls `SDL_SetWindowIcon`; `EngineRuntime::set_window_icon` wraps it the same way
`create_window` wraps `WindowSystem::create`.

Call site: `Engine::init`, **after** `assets_->load_catalog(...)`, alongside the existing
texture/`UiImage` preload loop — not at `create_window` time, because the catalog is not loaded
yet when the window is created.

| Platform | Behavior |
| --- | --- |
| Windows / Linux (X11) | `SDL_SetWindowIcon` works: title bar + taskbar. |
| macOS | No-op. SDL does not set the Dock icon this way; only the bundle icon (§19.2) does. |
| Web / Android | No-op. No windowed chrome to put an icon on. |

### 19.2 Packaging icon per platform

One host tool (§19.3) turns one master PNG into every platform's native icon format at build
time — never at runtime, so none of this links into the shipped binary.

| Platform | Mechanism | File(s) consumed |
| --- | --- | --- |
| Windows | `engine_add_game` generates an `icon.rc` (`IDI_ICON1 ICON "icon.ico"`) and adds it to `target_sources` when `WIN32 AND EXISTS icon.ico`. | `.ico` |
| macOS | `if(APPLE AND EXISTS icon.icns)`: `add_executable(${target} MACOSX_BUNDLE ...)`, `MACOSX_BUNDLE_ICON_FILE`, resource with `MACOSX_PACKAGE_LOCATION "Resources"`. | `.icns` |
| Android | `mipmap-*/ic_launcher.png` merged in via the game's resource overlay (§19.5); `android:icon="@mipmap/ic_launcher"` added to `<application>` in the engine's `AndroidManifest.xml`. | flat legacy PNG per density (no adaptive layers — §17) |
| Web | `favicon.png` copied beside the Emscripten output (same mechanism as `engine_target_web_preload`); `<link rel="icon">` added to `cmake/web/shell.html`. | `.png` |
| Linux | Not covered — no `install()` target exists for games yet (§17). | — |

`.ico` and `.icns` are not opaque platform-proprietary formats needing a platform-specific
encoder — both are containers that wrap already-decoded PNGs behind a small binary header
(`ICONDIR`/`ICONDIRENTRY` for `.ico`; `icns` + tagged chunks such as `ic07`/`ic08`/`ic09` for
`.icns`, both accepting embedded PNG payloads on any target OS since Vista / 10.7). That is why
one cross-compiled host tool can emit both, on any host, without shelling out to `rc.exe` or
`iconutil`.

`engine_add_game` runs `icon_codegen` **once**, shared by every consumer below, rather than each
platform block invoking the tool itself (four independent `add_custom_command`s writing
overlapping output files would race/duplicate). Gate: `EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"`.
When present, it emits one `add_custom_command` (`OUTPUT` listing all eight generated files) plus
an `${target}_icons` custom target the game target depends on, and records the output directory
on `ENGINE_GAME_ICON_DIR` (a target property on `${target}`). Each platform block below reads
`ENGINE_GAME_ICON_DIR` and `add_dependencies(${target} ${target}_icons)` instead of re-deriving
the path or re-invoking `icon_codegen`.

### 19.3 `icon_codegen` (host tool)

Same shape as `asset_codegen` / `asset_guid` (§10.4): built from
`tools/icon_codegen/main.cpp`, linked against `engine` for `decode_png_rgba`, invoked via
`add_custom_command` at configure/build time, never shipped. Cross-compiling (Android NDK)
requires a native build first and `-DENGINE_HOST_ICON_CODEGEN=/path/to/icon_codegen`, mirroring
`ENGINE_HOST_ASSET_CODEGEN`.

Input: one master PNG (minimum 1024×1024, square) named by convention, e.g. `icon.png` at the
game's root. Resizing uses a vendored `stb_image_resize2.h` (public domain, dropped into
`src/resources/` next to `stb_image.h` — no new submodule). Output, under
`${CMAKE_CURRENT_BINARY_DIR}/generated/<target>/icons/`: `icon.ico` (16/32/48/256),
`icon.icns` (16 up to 1024), `mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png`, `favicon.png`.

### 19.4 Android per-game identity overlay

`cmake/android/app/src/main/AndroidManifest.xml` and
`cmake/android/app/src/main/res/values/strings.xml` are engine-owned and fixed — without an
overlay every game would get the same `app_name`, `applicationId` (`org.windengine.app`), and
launcher icon, so two games cannot coexist as-is. Two independent mechanisms fix this, chosen
per resource kind rather than one uniform overlay — an earlier draft of this section proposed a
single `res.srcDirs` overlay for all three and was wrong about how AGP actually merges resources
(caught by a downstream game build failing for real; see below):

- **`applicationId`** — `ENGINE_ANDROID_APPLICATION_ID` (`-P` property, same pass-through style as
  `ENGINE_ANDROID_ASSETS_OUT`) overrides `defaultConfig.applicationId`; defaults to
  `org.windengine.app` when absent. Plain Gradle config, no resource merging involved.
- **`app_name`** — *not* a `values/strings.xml` overlay. `ENGINE_ANDROID_APP_NAME` feeds
  `defaultConfig.manifestPlaceholders = [appName: gameAppName ?: 'Wind']`, and the manifest's
  `<application>`/`<activity>` carry `android:label="${appName}"` — a manifest-merger token
  substitution, not a resource. A `values/strings.xml` placed in a game's own res dir and merged
  via `sourceSets.main.res.srcDirs += [...]` does **not** override the engine's own
  `string/app_name` the way a Unity-style plugin `res/` overlay might suggest: AGP only defines
  real override precedence between build-variant source sets (a product flavor or build type over
  `main`), not between multiple directories added to the *same* source set's `res.srcDirs` list —
  those merge as siblings, and AAPT2 hard-fails the build with "Duplicate resources" the moment
  two of them declare the same `string/app_name`. `manifestPlaceholders` sidesteps resource
  merging entirely.
- **`mipmap-*/ic_launcher.png`** — `ENGINE_ANDROID_RES_DIR` (a directory the game supplies) is
  added to `sourceSets.debug.res.srcDirs` **and** `sourceSets.release.res.srcDirs`, not
  `sourceSets.main.res.srcDirs`. A game that supplies no icon has nothing to resolve
  `android:icon="@mipmap/ic_launcher"` against otherwise, which is a hard AAPT2 link error, not a
  graceful fallback to a platform default — so the engine ships its own default
  `mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` under `cmake/android/app/src/main/res/`, the same
  role `strings.xml`'s built-in `app_name = "Wind"` already played for the label before
  `manifestPlaceholders` took over that job. That default living in `main`'s own `res/` is
  exactly why the overlay can no longer sit in `main.res.srcDirs` either, the way an earlier
  revision of this section had it and this one first shipped it: once `main` declares
  `mipmap/ic_launcher` itself, a sibling directory in the *same* source set declaring it again is
  the identical "Duplicate resources" collision `app_name` hit above — build-variant source sets
  are the only place with real override precedence over `main`, for icons the same as for names.
- `cmake/android/app/build.gradle` also threads `ENGINE_HOST_ICON_CODEGEN` into
  `externalNativeBuild.cmake.arguments` alongside the existing `ENGINE_HOST_ASSET_CODEGEN`
  passthrough — cross-compiling for Android needs a native `icon_codegen` (§19.3) the same way it
  needs a native `asset_codegen`, and this was missing until a downstream game's build hit it.

### 19.5 Testing

`WindowSystem::set_icon`'s `TextureDesc → SDL_Surface` conversion (dimensions, pitch, byte
layout) is a pure-data unit test, no window needed; `IGame::window_icon()`'s `nullopt` default
is a one-line contract test. `icon_codegen`'s `.ico`/`.icns` writers are tested by round-tripping
known PNG fixtures through the container writer and checking header/chunk bytes, not by
rendering — no OS icon viewer runs in `engine_tests` (§12.3). The `android:icon` attribute, the
`ENGINE_ANDROID_RES_DIR`/`ENGINE_ANDROID_APPLICATION_ID`/`ENGINE_ANDROID_APP_NAME`/
`ENGINE_HOST_ICON_CODEGEN` plumbing, the `manifestPlaceholders`/`android:label="${appName}"`
wiring, and the presence of the default `mipmap-*/ic_launcher.png` set all get text-content
regression tests in [[tests.cmake_sanity_test.cpp]], the same pattern as
`AndroidManifestDeclaresVibratePermission` (§18.4) — there is no real Gradle resource-merge
build in this repo's CI to exercise instead, which is exactly how the `res.srcDirs` overlay claim
in §19.4 went uncaught until a downstream game's real build failed on it. The macOS bundle path
(§19.2) has no test at all, same caveat as noted in §17.

---

## 20. Splash screen

A runtime concern, not a packaging one (§19) — shown by `Host`/`EngineRuntime` right after the
window exists, using an asset already in the NanoVG image cache, not something CMake generates.
Default: **on**, showing the engine's own mark, fading in from black and back out to black.

### 20.1 Explicit, window-aware trigger

```cpp
struct SplashScreen {
    bool enabled = true;
    AssetId image = builtin::splash_wind;
    float fade_in_seconds = 0.4f;
    float hold_seconds = 1.0f;
    float fade_out_seconds = 0.4f;
};
```

`SplashScreen` is pure config data — a game overrides `image` for its own splash, tunes the three
durations, or sets `enabled = false` to skip it outright. No single `duration_seconds` — three
phases, because the animation (§20.3) needs fade-in and fade-out timed independently from the hold
in the middle. Unlike `window_title()` / `window_size()` / `window_icon()` (§5, §19.1), it is not
reached through an `IGame` virtual: the engine never triggers the splash on its own. Instead
`include/engine/ui/splash.h` exposes one free function a game calls explicitly:

```cpp
struct SplashTimer {
    float elapsed = 0.0f;
    float total_duration = 0.0f;
};

[[nodiscard]] std::optional<ecs::Entity> show_splash(
        ecs::World& world, const SplashScreen& config, glm::vec2 image_size, WindowId window = kPrimaryWindow);
```

Typically called from `on_start()` (closest analogue to the old auto-triggered behavior), but
nothing requires that — it can be called later, e.g. right after opening a secondary window via
`IWindowControl` (§21.3), targeting that window instead of the primary. `image_size` is the
configured image's real decoded pixel size, resolved by the caller from
`AssetsDb::get<render::TextureDesc>(config.image)` — the same DI-provided `AssetsDb` a game
already has for loading any other asset; the engine keeps no `AssetId → glm::vec2` cache of its
own for this. `window` (default `kPrimaryWindow`) lets a game target a secondary window (§21.6)
instead of always the primary. `show_splash` returns `nullopt` when there's nothing to show
(`enabled == false`, or `build_splash_document` fails — §20.3's zero-duration/unresolved-image-size
cases), otherwise the spawned entity, which carries `UiCanvas` + `UiInstance` + `SplashTimer{
total_duration = fade_in_seconds + hold_seconds + fade_out_seconds}`. An engine system
(`run_splash_timers`, registered in `register_engine_systems`, §20.3/§20.5) ages every
`SplashTimer` and despawns its entity automatically once `total_duration` elapses — the caller
does not need to track or destroy it itself.

### 20.2 Default asset

Builtin, like the default shader / quad / material / UI font (§10.8) — not something
`icon_codegen` touches, since that pipeline is for per-game packaging icons, and this is a single
engine-owned runtime image:

- `builtin_assets/textures/splash.png` + `.meta`:
  ```toml
  guid = "a0e1b2c3d4f5678901234567890abc05"
  importer = "ui_image"
  color_space = "srgb"
  filter = "linear"
  wrap = "clamp"
  layout = "single"
  ```
- `include/engine/builtin_ids.h`: `builtin::splash_wind`, the 5th well-known GUID, added to
  `ids` / `count()`. Existing four GUIDs are **not** renumbered (§10.8: never regenerate).
- `importer = "ui_image"`, not `"texture"`: it's drawn through the same NanoVG image path UI
  images already use, which `Engine::init()`'s existing preload loop (the one that already walks
  `Texture`/`UiImage` catalog entries into `runtime_.add_image`) picks up automatically — no new
  loading code needed, the splash is just another entry in the engine's own builtin catalog.
- Source art: an AI-generated "made with WindEngine" mark, cleaned up before committing —the
  original export had a broken alpha channel (jagged, never fully opaque, from a bad
  background-removal pass) sitting over otherwise-clean RGB; flattened to opaque RGB fixed it
  with no visible seam. Then cropped tight to the content region (the source canvas had roughly
  a third of its width as dead black margin on each side) so the shipped asset isn't mostly empty
  space before any runtime letterboxing even happens.

### 20.3 Rendering: reuse the CSS keyframe animation system, not a new draw path

The per-frame tick does **not** gate `Schedule::Fixed`/`Frame`, and `on_start()` is **not**
moved — both stay exactly as they are today. Neither `EngineRuntime` (`src/core/engine_runtime.cpp`,
the real production loop) nor `Host` (`include/engine/core/host.h`, the separate parallel class
exercised only by `tests/host_test.cpp`) spawns or ages the splash itself — that used to matter
when the trigger lived in `EngineRuntime::begin_loop()`/`tick_loop()`, but now the whole mechanism
is ECS-native: `ui::show_splash` (§20.1) is a plain function a game calls, and the aging system
(`run_splash_timers`, §20.5) is registered by `register_engine_systems`, which both `Host` and
`Engine<GameT>::init()` already call — so it works identically from either entry point with no
special-casing. The splash is not a pause-like blocking phase; it is one more
`UiCanvas`/`UiInstance` entity drawn on top of everything else, fading itself out via machinery
that already exists:

- The UI layer already has a working `@keyframes` opacity animator: `ComputedStyle::animation_name`
  / `animation_duration`, `Element::animation_elapsed` (accumulates real per-frame `delta_time`,
  clamped to the duration — see `apply_animation_opacity`/`sample_opacity` in `src/ui/paint.cpp`),
  and an `Image` element kind (`ElementKind::Image`, draws via the same `IUiPainter::image` +
  `set_opacity` `paint_element` already uses for any element's `opacity`). Fade-in/hold/fade-out
  is a 4-stop keyframe list computed once from the config
  (`{0%: 0}, {fade_in/total%: 1}, {(fade_in+hold)/total%: 1}, {100%: 0}`, `animation-duration =
  fade_in+hold+fade_out`) — no new animation code, no new painter method.
- This document/stylesheet is **not** a builtin XML/CSS asset (the percentages depend on the
  game's runtime `SplashScreen` config, which a static asset can't parameterize) — build the CSS
  and XML as small formatted strings from the config and feed them through the existing
  `ui::parse_xml` / `parse_css`, the same functions already used everywhere else text markup
  becomes a `Document`/`Stylesheet`, rather than hand-assembling `Element`/`Keyframes` structs.
  §16 rule 11 ("games do not build visual trees in C++") is about the API surface exposed to game
  authors — the engine procedurally generating its *own* one fixed internal splash document from
  a config struct is a narrow, documented exception to that rule, not a pattern games are meant to
  copy.
- Spawned by `ui::show_splash` (§20.1) — gated on `config.enabled` — as a `UiCanvas{fit =
  UiFit::ScaleWithScreenSize, order = <high, above every other canvas>, window}` +
  `UiInstance{document, stylesheet}` + `SplashTimer{total_duration}` entity. A game typically calls
  it right where the old auto-trigger used to fire (around `on_start()`, after
  `Engine::init()`'s catalog/image-preload loop has finished, so the builtin/game splash image is
  already resolved through `AssetsDb`), but the call site is entirely the game's choice now — §20.1.
  `world.create()` + `world.emplace<ui::UiCanvas>(...)` + `world.emplace<ui::UiInstance>(...)` is
  the existing spawn pattern — see `spawn_button_canvas` in `tests/mvvm_test.cpp` for a working
  example of building a `UiCanvas` + `UiInstance{parsed_document}` pair from a `parse_xml` result.
  `run_ui_render` (`src/ecs/systems.cpp`, the existing `Phase::UiRender` system) already walks
  every `UiCanvas`/`UiInstance` entity and pushes its draw calls through the same `CommandBuffer`
  → render-backend path everything else uses — no `ICanvas`/`OpenGLCanvas` changes needed.
- **Aspect ratio, not stretch-to-fill**: the `Image` rule is `position: absolute; left: 10%;
  top: 10%; width: 80%; height: 80%` — a fixed, centered 80% box within the canvas, not
  `width/height: 100%` (a first draft used `100%`, which stretches a non-square image to whatever
  aspect ratio the window happens to be — wrong, caught after implementation and fixed). The
  actual letterboxing that keeps the image's own aspect ratio comes from `UiCanvas::reference_size
  = image_size / 0.8` combined with `ScaleWithScreenSize`'s existing contain-fit math
  (`canvas_layout_space`/`scaled_fit_rect` in `src/ui/canvas.cpp`, §8.1's `ScaleWithScreenSize`
  entry): that fits the *canvas* into the real window preserving `reference_size`'s aspect ratio
  (the image's own), so the fixed 80% image box inside it lands at the image's exact aspect ratio
  with a minimum 10% margin on every edge — more margin on whichever axis the window's aspect
  ratio doesn't match, never less. `image_size` is the configured image's real decoded pixel
  dimensions, not something `build_splash_document` can know on its own — the caller resolves it
  via `AssetsDb::get<render::TextureDesc>(config.image)` and passes it into `ui::show_splash`
  (§20.1); the engine keeps no `AssetId → glm::vec2` cache of its own for this.
- One divergence from `spawn_button_canvas`'s literal shape: that test never runs `Phase::Bind`,
  so it can set `canvas.document` to an arbitrary/dummy `AssetId` without consequence. In the real
  loop, `run_bind` (`src/ecs/systems.cpp`) runs every frame and calls `clone_document` — which
  replaces `UiInstance` with a fresh `assets.get<UiDocument>(canvas.document)` — whenever
  `instance_needs_rebuild` sees `UiInstance::loaded_document != UiCanvas::document` (plus
  stylesheet/data-context). The splash's document only exists in memory, so `canvas.document` and
  `canvas.data_context` must stay at their defaults (matching `UiInstance`'s equally-defaulted
  `loaded_document`/`loaded_data_context`) to keep that check a no-op — otherwise the in-memory
  document gets silently clobbered by a failed asset lookup on the very next frame.
- **The game underneath is already running while the splash shows, so its own root needs a
  constant opaque backdrop, and that backdrop must be explicitly despawned when it's over** —
  this was wrong in the first implementation, caught by the game visibly flashing behind the
  splash for its first frame. `on_start()` and `Schedule::Fixed`/`Frame` are never gated (§20.3's
  opening paragraph), so the game's own UI (a menu, say) is already fully set up and rendering by
  the time the splash spawns on top of it; on the splash's own first frame the `Image`'s
  `animation_elapsed` is still 0 (opacity 0, per the keyframe stops above), so with nothing else
  drawn by the splash, the game shows through underneath for the whole fade-in ramp. The fix: the
  root `<Canvas>` gets its own rule with a **constant** `background: #000000` — no
  `animation-name`, so it stays fully opaque for the splash's entire lifetime rather than fading
  with the image — while the `Image` child keeps the keyframe animation from above. That opaque
  root then has to be explicitly removed once the sequence ends, or the game stays permanently
  blacked out after the image's fade-out finishes: `element.animation_elapsed` clamps at
  `animation_duration` and the image's last keyframe stop is `opacity: 0`, so the *image* sitting
  invisible forever would be fine on its own, but the *non-animated, always-opaque* root
  backdrop never goes away by itself. The despawn is ECS-native, not engine-internal hidden state:
  `ui::show_splash` emplaces a `SplashTimer{elapsed = 0, total_duration}` on the spawned entity
  (separate from `element.animation_elapsed`, which is the UI painter's own per-element concept),
  and `run_splash_timers` — a system registered in `register_engine_systems`
  (`ecs::Schedule::Frame`, `ecs::Phase::Input`, `src/ecs/systems.cpp`) — adds real
  `Time::delta_time` to every `SplashTimer::elapsed` each frame and calls `world.destroy(entity)`
  once `elapsed >= total_duration` — despawning was floated as optional tidiness in an earlier
  draft of this section; it is not optional once the backdrop is opaque and constant.
- No canvas-clear-color change needed for the letterbox bars specifically: `OpenGLCanvas::draw()`
  already clears to black (`glClearColor(0,0,0,1)`) every frame regardless, so the area the
  `ScaleWithScreenSize`-fit canvas rect doesn't cover is already black without special-casing —
  it's only the *inside* of that canvas rect, covered by the game's own already-rendered UI,
  that needed the explicit opaque backdrop above.

### 20.4 Open question — not v1

Fixed durations assume `Engine::init()`'s synchronous catalog/font/image loading (already
finished before the splash phase even starts) is the only thing worth covering. Tying the splash
to real load completion instead of a timer is a possible follow-up, not implemented here — same
status as the deferred items in §17.

### 20.5 Testing

The keyframe-animator half (`element.animation_elapsed` accumulation, `sample_opacity`'s
interpolation between stops) is **already** covered by `UiPainter.KeyframeOpacityAdvancesWithDeltaTime`
(§20.3) — this feature adds no new animation logic to test. What's new and needs its own coverage:
building the 4 keyframe stops (`{0, 0}`, `{fade_in/total, 1}`, `{(fade_in+hold)/total, 1}`,
`{1, 0}`) and `animation-duration = fade_in+hold+fade_out` from a `SplashScreen` config — a pure
function, testable directly (feed it a config, assert the stop offsets/values and duration), no
window or painter needed. Feed the generated CSS/XML strings through the real `parse_css`/
`parse_xml` in the test too, not just the raw computed numbers, so a malformed generated string
is caught the same way `UiXml.UnknownElementIsFatal`-style tests already catch bad markup.
`SplashScreen`'s defaults (`enabled = true`, `image = builtin::splash_wind`, the three durations)
are a one-line contract test in `tests/window_icon_test.cpp`
(`SplashScreenContract.DefaultsMatchSdd`) — since `SplashScreen` is no longer reached through an
`IGame` virtual (§20.1), it's just `const engine::SplashScreen splash;` constructed directly.
Whether the spawned `UiCanvas`/`UiInstance` entity actually renders on top of everything else is
not tested — GPU/window excluded from `engine_tests` (§12.3), same as everything else in §19 and
§18 — but that `ui::show_splash` spawns the entity at all when `enabled` and returns `nullopt`
without spawning anything when disabled is an ECS-level check (`world.view<ui::UiCanvas>()`
count, plus asserting the returned entity carries a `SplashTimer`), no window needed, same spirit
as `tests/host_test.cpp`'s existing `Host` construction tests; `tests/splash_test.cpp` also checks
that `window` defaults to `kPrimaryWindow` and is threaded onto `UiCanvas::window` when a caller
passes a different one (§21.6). The root's constant `background: #000000` rule and its lack of an
`animation-name` are checked the same pure-function way as the image rule (find the rule declaring
`background`, assert no `animation-name` on it) — the point being that it must *not* fade with the
image. `run_splash_timers` itself is ECS-level testable too, unlike the old hidden
`EngineRuntime`-internal timer it replaces: spawn (or `world.emplace`) a `SplashTimer`, call
`engine::register_engine_systems(world)` + `world.run(ecs::Schedule::Frame)` with a known
`ctx<Time>().delta_time` (same idiom as `tests/host_test.cpp`'s `PhaseOrderFrame`/`PhaseOrderFixed`
tests), and assert `world.valid(entity)` stays true while `elapsed < total_duration` and flips
false once accumulated `delta_time` crosses it — no window or real loop iteration needed.

---

## 21. Windowing

A desktop-overlay companion game (game runs as a borderless, transparent, always-on-top strip
near the taskbar; separate popup windows for per-building detail) needs three things this engine
did not have: window *style* control beyond a fixed decorated rectangle, mouse pass-through so
the desktop underneath stays usable, and more than one OS window per process. §2.2 used to list
multi-window as a v1 non-goal; this section replaces that with a real design.

### 21.1 Design rationale

**One `World`, many windows — not one `World` per window.** §4.3's "one world, no Node graph"
rule is not window-specific by accident: a building's detail popup reads the same `ViewModel`
and the same `ecs::World` as the overlay, it just paints into a different `WindowId`. Giving each
window its own `World`/`Loop` would duplicate `Time`/`ApplicationState`/schedule wiring for no
benefit here, and would blur into the already-rejected "sharing one process between multiple
games" non-goal (§2.2) — that non-goal is about running two independent `IGame`s, not one game
with two windows. So: a window is an output target a `UiCanvas` (or, for `kPrimaryWindow`, world
`Renderable`s) is drawn into, not a second simulation.

**Breaking `IGame` is fine.** This engine is pre-1.0 with no external consumers to freeze an ABI
for (§0 constraints); `primary_window()` replacing `window_title()`/`window_size()` (§5) is a
straight rename-and-merge, not a deprecation path. Windowing work in general should prefer the
clean shape over a compatibility shim.

**Scope for v1:** one *primary* window that also renders the game world, plus any number of
*secondary* windows that are UI-only (a building's stat panel, the initial settings window before
the overlay appears). A secondary window is not a second `Camera`/world viewport — see the open
item in §17.

### 21.2 `WindowDesc` / `WindowStyle`

Public, GL/SDL-free (`include/engine/core/window_desc.h`):

```cpp
struct WindowStyle {
    bool borderless = false;
    bool always_on_top = false;
    bool transparent = false;     // needs SDL_WINDOW_TRANSPARENT at creation; can't be added later
};

struct WindowDesc {
    std::string title = "Game";
    glm::ivec2 size = {800, 600};
    std::optional<glm::ivec2> position;   // nullopt = platform default placement
    WindowStyle style;
};

enum class WindowId : std::uint32_t {};
inline constexpr WindowId kPrimaryWindow{0};
```

`transparent` is create-time only (SDL requires `SDL_WINDOW_TRANSPARENT` in the creation flags;
there is no "make an existing window transparent" call) — a game that wants to switch from an
opaque settings window to a transparent overlay opens a **second** window and destroys the first,
it does not flip a flag on one window (matches the "first window 500×600 settings → hide it,
show a transparent overlay" use case directly: that is `create_window` + `destroy_window`, not a
style mutation).

When `style.transparent` is set, that window's canvas clears to `(0, 0, 0, 0)` instead of the
engine-wide opaque black (§4.7). `borderless` and `always_on_top` map straight to
`SDL_WINDOW_BORDERLESS` / `SDL_SetWindowAlwaysOnTop` and, unlike `transparent`, can be toggled at
runtime (§21.3).

**`SDL_WINDOW_BORDERLESS` alone does not produce a chrome-less window on Windows.** SDL3's Windows
backend defaults a borderless window to `STYLE_BORDERLESS_WINDOWED` (`WS_POPUP | WS_CAPTION |
WS_SYSMENU | WS_MINIMIZEBOX`, `external/SDL3/src/video/windows/SDL_windowswindow.c`,
`GetWindowStyle()`) — i.e. Windows still draws a titlebar — deliberately, so a borderless window
keeps acting like a normal desktop citizen (shows in the taskbar, respects the work area). A
desktop-overlay window wants the opposite: no titlebar, no system menu, at all. This was found by
actually running a downstream game (`td-over`) and inspecting its live windows via
`GetWindowLongPtr(hwnd, GWL_STYLE)` — the "borderless" primary window's real style was
`0x96CF0000`, which includes `WS_CAPTION`/`WS_SYSMENU`, confirming the titlebar seen on screen was
not a rendering artifact. `WindowSystem::create()` (`src/render/opengl/window_system.cpp`) now
sets the (public-API-less, raw-string) hint before creating a borderless window:
```cpp
if (desc.style.borderless) {
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0");
}
```
which selects SDL's other borderless style (`STYLE_BORDERLESS = WS_POPUP | WS_MINIMIZEBOX`, no
caption/sysmenu). Re-inspecting the same live window afterward gave `0x96070000` — `WS_CAPTION`
and `WS_SYSMENU` gone, everything else (resizable/minimize bits) unchanged — confirming the fix
empirically, not just by reading source. The hint is read once per `SDL_CreateWindow` call, so
setting it immediately before creating *this* window is sufficient and has zero effect on any
window created with `borderless = false` (that branch in `GetWindowStyle()` is never taken for
one), including a different window created earlier or later in the same process.

`WindowSystem::is_transparent()` reports whether the live window was created with
`style.transparent` (`false` before any `create()` and after `destroy()`) — `OpenGLCanvas::draw()`
reads it each frame to pick the clear alpha (§4.7), rather than re-deriving it from `WindowDesc`.

`SDL_WINDOW_TRANSPARENT` by itself is not sufficient on Windows: DWM only composites a GL window's
backbuffer with alpha if the window's own GL pixel format actually carries an alpha channel, and
SDL3 does not add one automatically for a transparent window. Read from source
(`external/SDL3/src/video/windows/SDL_windowswindow.c` and `SDL_windowsopengl.c`, read-only per
that submodule's own no-AI-contributions `CLAUDE.md`): on Windows, `SDL_WINDOW_TRANSPARENT` only
makes `WIN_CreateWindow` call `DwmEnableBlurBehindWindow` with a zero-size blur region — a trick
that lets DWM alpha-blend the window against the desktop using the backbuffer's own alpha, without
`WS_EX_LAYERED`/`SetLayeredWindowAttributes`. It never touches `_this->gl_config.alpha_size`; the
WGL pixel-format selection in `SDL_windowsopengl.c` (`WIN_GL_ChoosePixelFormat*`) sets
`cAlphaBits`/`WGL_ALPHA_BITS_ARB` purely from `gl_config.alpha_size`, which is zero unless the app
calls `SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8)` beforehand. So `WindowSystem::create()` requests
`SDL_GL_ALPHA_SIZE = 8` before `SDL_CreateWindow` whenever `desc.style.transparent` is set (this is
also where the pixel format is actually finalized on Windows — inside `SDL_CreateWindow` itself,
before any `SDL_GL_CreateContext` call) and only in that case, to avoid changing the pixel format
opaque windows get. `OpenGLCanvas::init()` mirrors the same attribute before `SDL_GL_CreateContext`
(via `window_->is_transparent()`) purely for attribute-consistency with `WindowSystem::create()`,
the same way it already mirrors `SDL_GL_DOUBLEBUFFER` — the pixel format itself is already fixed by
that point.

Validated on Windows only for v1 (DWM composites unconditionally since Windows 8, so
`SDL_WINDOW_TRANSPARENT` behaves predictably); Linux depends on the running compositor and macOS
is untested here — same "not blocking, not regressed against" status as the macOS icon path in
§17.

**Known report, not yet fixed:** a downstream game (`td-over`) reported a primary window created
with `borderless = true, transparent = true` rendering opaque black instead of see-through.
Re-reading the whole pipeline end to end for this found nothing incorrect on paper:
`window_style_flags()` ORs `SDL_WINDOW_BORDERLESS`/`SDL_WINDOW_TRANSPARENT` independently (no
masking bug when both are set together), `WindowSystem::create()` requests `SDL_GL_ALPHA_SIZE = 8`
unconditionally whenever `desc.style.transparent` regardless of `borderless`, and
`OpenGLCanvas::draw()` reads `is_transparent()` off the same `WindowSystem` instance that just
created the live window, each frame, not a stale one. SDL3 itself already calls
`DwmEnableBlurBehindWindow(hwnd, &bb)` with a degenerate/empty blur region
(`CreateRectRgn(-1, -1, 0, 0)`) for `SDL_WINDOW_TRANSPARENT`
(`external/SDL3/src/video/windows/SDL_windowswindow.c`, ~line 758-775) — the textbook-correct
technique for whole-window-alpha compositing without an actual blur effect — so this pipeline looks
complete and correct on paper, yet the symptom is real and reproducible in a live game. No fix has
been applied yet pending further diagnosis; see the §17 open item.

### 21.3 Runtime window control

For the single-window shape that exists today (§21.5 below is what generalizes this to several
windows), a game reaches window control the same way it reaches any other engine service —
DI-constructor injection (§4.2), not a locator:

```cpp
// include/engine/core/window_control.h — public, GL/SDL-free
class IWindowControl {
public:
    virtual ~IWindowControl() = default;
    virtual void set_borderless(bool borderless) = 0;
    virtual void set_always_on_top(bool always_on_top) = 0;
    virtual void set_position(glm::ivec2 position) = 0;
    virtual void resize(glm::ivec2 size) = 0;      // WindowStyle::transparent has no setter — create-time only (§21.2)
};
```

`Engine<GameT>::init()` binds `IWindowControl` in the injector to `EngineRuntime::window_control_ptr()`
— a small private `WindowControlImpl` (`src/render/opengl/window_control.h`) that forwards each
call to the one `WindowSystem` `EngineRuntime` owns (`SDL_SetWindowBordered`, `SDL_SetWindowAlwaysOnTop`,
`SDL_SetWindowPosition`, `SDL_SetWindowSize`) — the same shared-instance pattern already used for
`ICanvas`/`OpenGLCanvas` (§4.2). A game constructor that wants to expose "always on top" and
"borderless" toggles in a settings menu takes `IWindowControl&`, same as it would take
`IAudioSystem&`. `IGame::primary_window()` (§5) only *declares* the window that must exist before
`on_start`; changing it afterward goes through `IWindowControl`, not a second call to
`primary_window()`.

Multi-window (§21.5/§21.7) generalizes part of this to `WindowId`-addressed calls:
`IWindowControl::open_window(const WindowDesc&) -> std::optional<WindowId>` and
`close_window(WindowId)`, thin forwards to `WindowManager::create_window`/`destroy_window`, are the
first (and so far only) `WindowId`-addressed methods on the interface — a game finally has a
DI-reachable way to invoke §21.5's multi-window infrastructure at all. Everything else on
`IWindowControl` (`set_borderless`, `set_always_on_top`, `set_position`, `resize`,
`set_click_through_enabled`, `set_drag_region`) still implicitly means `kPrimaryWindow` only —
generalizing those is deferred, consistent with this feature area's incremental scoping.

Because `open_window`/`close_window` need the whole `WindowManager`, not just the primary
`WindowSystem`, `WindowControlImpl`'s constructor changed from `WindowControlImpl(WindowSystem&)`
to `explicit WindowControlImpl(WindowManager&)` (`src/render/opengl/window_control.h`) — its other
five methods now go through `windows_->primary_window()` instead of a bare `WindowSystem&`. The one
construction site, `EngineRuntime::Impl` (`src/core/engine_runtime.cpp`), passes `windows` (the
manager) instead of `windows.primary_window()`.

### 21.4 Click-through overlay mode

Goal: while the overlay sits always-on-top and transparent over the desktop, clicks over the
transparent parts of the window must reach whatever is underneath (the taskbar, the user's IDE),
while clicks over an actual game widget or sprite still hit the game.

**Mechanism (Windows):** `WindowSystem::apply_click_through` (`src/render/opengl/window_system.cpp`)
reads the window's native handle via `SDL_GetWindowProperties` /
`SDL_PROP_WINDOW_WIN32_HWND_POINTER` and toggles the `WS_EX_TRANSPARENT` extended style on that
HWND — `SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT)` to pass clicks through,
clear the bit to capture them again. Confirmed from `external/SDL3/src/video/windows/SDL_windowswindow.c`
that plain `WS_EX_TRANSPARENT` is sufficient here without `WS_EX_LAYERED`: this window's alpha
compositing already goes through DWM's `DwmEnableBlurBehindWindow` (§21.2's `SDL_WINDOW_TRANSPARENT`
path, in `WIN_CreateWindow`), not the legacy layered-window path — `WS_EX_LAYERED` only appears
elsewhere in that file, inside `WIN_SetWindowOpacity`, and is unrelated/independent from the
`WS_EX_TRANSPARENT` hit-test-passthrough bit. This is a Win32 detail: it lives entirely inside
`WindowSystem` (`src/render/opengl/window_system.h`/`.cpp`) behind `#if defined(_WIN32)` (a
no-op `#else` branch elsewhere), `#include <windows.h>` only in the `.cpp`, never a public type
(§16 rule 15).

**Decision per frame:** reuse the existing hit-test result instead of a new pipeline.
`UiInputSystem` (§4.3) already computes, per window, whether the pointer is over a widget
(`MouseConsumed`); v1 click-through is **bounding-box** — the window is click-through whenever
that per-window hit-test misses, and captures input whenever it hits. This is *not* per-pixel
alpha sampling of the rendered frame (that needs a framebuffer readback synced against the GL
swap, deferred — §17); a fully transparent pixel inside a widget's bounding rect still captures
input in v1, same as an opaque one.

```cpp
bool should_be_click_through(bool click_through_enabled, bool window_is_transparent, bool pointer_hit_something) noexcept;
```

(`src/render/opengl/window_system.h`, next to `window_style_flags` for the same reason — SDL-type-free
and unit-testable without `SDL_Init`) is a pure function
(`click_through_enabled && window_is_transparent && !pointer_hit_something`).
`WindowSystem::update_click_through(bool pointer_hit_something)` calls it once per frame — using
`click_through_enabled()`/`is_transparent()` for the other two inputs — and applies the result via
`apply_click_through` only when it changed since the last frame (a cached `click_through_applied_`
member avoids calling `SetWindowLongPtrW` every frame when nothing changed). `EngineRuntime::tick_loop()`
(`src/core/engine_runtime.cpp`) calls `impl_->window.update_click_through(world.ctx<ui::MouseConsumed>().value)`
right after `game.on_update()` returns, once `Phase::Input` has run for the frame and
`MouseConsumed` is current, and before `canvas().draw()`.
`IWindowControl::set_click_through_enabled(bool)` (`include/engine/core/window_control.h`,
forwarded by `WindowControlImpl` to `WindowSystem::set_click_through_enabled`) is the manual on/off
a settings menu binds to; the automatic per-frame toggle only runs while it's enabled and the
window is transparent. Once `WindowId`-addressed multi-window control lands (§21.5, not yet
built), this generalizes to a per-window call the same way the rest of `IWindowControl` will.

### 21.5 Multi-window infrastructure

`WindowId` (public, `core/window_desc.h`) is `enum class WindowId : std::uint32_t {}` with
`inline constexpr WindowId kPrimaryWindow{0}`; `enum class` gets `std::hash`/`==` for free since
C++14 (LWG 2148), so it works as an `unordered_map` key with no extra machinery.

`WindowManager` (private, `src/render/opengl/window_manager.h`/`.cpp`) owns one
`{WindowSystem, CommandBuffer, OpenGLCanvas}` triple per live `WindowId`, keyed in an
`unordered_map<WindowId, unique_ptr<Entry>>` (`unique_ptr<Entry>` because `OpenGLCanvas` captures
`WindowSystem&`/`CommandBuffer&` by reference at construction — a bare `Entry` value in the map
would dangle on rehash). All triples share the one `IRenderBackend`/`IGraphicFactory`-produced GL
objects `EngineRuntime` already owns — texture/mesh/shader GL object ids stay valid across the
whole GL share group once contexts share, so `AssetsDb`/`IGraphicFactory` stay single-instance;
nothing about asset upload changes.

**The `kPrimaryWindow` slot is permanent infrastructure, not just another map entry.**
`WindowManager`'s constructor eagerly builds the primary `Entry` (its `WindowSystem` default-
constructs with no OS window yet; its `CommandBuffer` and `OpenGLCanvas` are constructed too, the
latter inert until `init()` runs) and the map entry is **never erased** for the lifetime of the
`WindowManager` — only reset in place:

- `create_primary_window(desc)` calls `entry.window.create(desc)` (which itself calls `destroy()`
  first) then `entry.canvas->init(/*with_ui_painter=*/true)` on the *same* `Entry` object, tearing
  down and rebuilding the OS window/GL context in place rather than swapping in a new `Entry`.
- `destroy_window(kPrimaryWindow)` and `shutdown()` do the same reset-in-place (`window.destroy()`
  + a freshly constructed `canvas` bound to the same `window`/`commands`) instead of removing the
  slot. Secondary windows, by contrast, are fully erased (`unordered_map::erase`) — nothing outside
  `WindowManager` holds long-lived references into a secondary `Entry` in this phase.

This is why: `EngineRuntime::Impl`'s `WindowControlImpl` (bound to `windows.primary_window()`, a
`WindowSystem&`) and its `commands_ptr()`/`canvas_ptr()` (bound to `windows.commands_ptr()`/
`canvas_ptr(kPrimaryWindow)`, both `shared_ptr`) are all wired into `Engine<GameT>`'s DI graph
*before* `EngineRuntime::create_window` — i.e. `WindowManager::create_primary_window` — is ever
called (see `di::make_injector(...)` vs. the later `runtime_.create_window(...)` call in
`engine.h`). If the primary slot could be erased and reallocated at a different address (as a
literal "map key → value" model would suggest), those bindings would dangle the first time a game
re-created its primary window, or the first time `shutdown()` ran. Treating the primary slot as
permanent, mutated-in-place infrastructure is the same pattern the pre-this-phase `EngineRuntime`
already used (a plain `WindowSystem window;` member, `.create()`/`.destroy()`'d in place, never
reallocated) — this phase just moves that object inside `WindowManager` without losing the
guarantee.

`WindowManager`'s public surface reflects this split:

- `has_window(id)` / `window(id)` / `canvas(id)` / `commands(id)` are **liveness-gated** — `false`/
  `nullptr` unless a real SDL window exists for `id` (`entry->window.window() != nullptr`). This is
  what `tests/window_style_test.cpp`'s `WindowManager` cases exercise: on a freshly constructed
  manager (no `SDL_Init(SDL_INIT_VIDEO)`, §12.3), all four report "not found" for both
  `kPrimaryWindow` and an arbitrary other id, and `destroy_window`/`shutdown`/`draw_all` on that
  empty-but-primary-slotted manager do not crash.
- `commands_ptr(id)` / `canvas_ptr(id)` are **ungated** — valid whenever a slot exists at all,
  which for `kPrimaryWindow` is always. `primary_window()` returns `WindowSystem&` the same way,
  ungated, for `WindowControlImpl`'s sake.
- `create_window(const WindowDesc&)` (secondary) fails (`nullopt`) unless `has_window(kPrimaryWindow)`
  is already true — a secondary's GL context shares the primary's, so the primary must be live
  first. On success it inserts a *new* `Entry` into the map (ordinary insert, not the primary's
  reset-in-place dance) and returns a fresh `WindowId{next_id_++}`; `next_id_` starts at 1 and is
  **never reset**, including across `shutdown()`, matching this codebase's "never reuse a GUID"
  hygiene for asset ids, applied here by analogy so a stale `WindowId` can never silently refer to
  a different, later window.
- `draw_all()` iterates every entry and calls `canvas->draw()` **only when `window.window() !=
  nullptr`** — the primary's `canvas` object exists (and is safe to call methods on) even before
  any window was ever created, but calling `draw()` on it pre-`init()` would issue raw GL calls
  with no context loaded (`glClearColor`/`glClear` as null function pointers, since `glad` is only
  loaded inside `OpenGLCanvas::init()`) — a real crash risk this liveness check exists to prevent.

**Context sharing.** `create_window`'s sequencing, immediately before constructing the secondary's
`OpenGLCanvas` and calling `init(false)`:
```cpp
SDL_GL_MakeCurrent(primary->window.window(), primary->canvas->native_context());
SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
```
`SDL_GL_CreateContext` (called inside `OpenGLCanvas::init()`) consults
`SDL_GL_SHARE_WITH_CURRENT_CONTEXT` against whichever context is *current at that call*, per SDL's
own contract (`SDL_video.h`) — so the primary's context must be made current right before `init()`
runs, not merely at some earlier point. `OpenGLCanvas::native_context() const noexcept` (a small
`src`-only accessor next to its other simple getters, not part of the public API surface) exposes
the `SDL_GLContext` needed for the `SDL_GL_MakeCurrent` call.

**`OpenGLCanvas::init(bool with_ui_painter = true)`.** The default keeps every pre-existing call
site (`EngineRuntime`'s primary-window path via `WindowManager`; the already-dead
`OpenGLRuntime::init()` in `opengl_runtime.h`) compiling and behaving identically. `destroy_context()`
(called at the top of every `init()`, and from the destructor) was fixed alongside this: it used to
unconditionally call `gl_backend->set_ui_painter(nullptr)`, which — now that `backend_` is shared
across every window's `OpenGLCanvas` — would have nulled out *whichever* window's painter happened
to be currently set the instant another window's `init()`/destructor ran (`init()` always calls
`destroy_context()` first, even on a brand-new canvas that never owned a painter). It now only
clears the shared pointer when `ui_painter_ != nullptr`, i.e. when this particular canvas is the
one that actually set it.

**Per-window UI painting was solved without changing `execute()`'s signature — see §21.6.** This
section originally deferred the problem (a secondary window's `OpenGLCanvas` skipped NanoVG
entirely, `init(with_ui_painter=false)`) because `OpenGLRenderBackend::set_ui_painter` is a single
mutable pointer field on the *one* shared `backend_`, and a naive "just call it from every window's
`init()` too" would let the *last* window to `init()` silently win, permanently clobbering every
other window's painter. §21.6 fixes this a different way: every window still shares the one
`ui_painter_` field, but `OpenGLCanvas::draw()` now re-arms it to *its own* `NanoVgPainter` every
frame, immediately before its own `backend_->execute()` call — since `execute()` only ever reads
whatever painter is current *at that call* (never something captured earlier), "last window to run
wins" is scoped to the instant of that one `execute()` call rather than being a lasting corruption.
No `CommandBuffer`-keyed or per-window `IRenderBackend` state was needed.

**`EngineRuntime`** (`core/engine_runtime.h`/`.cpp`) exposes the primary path unchanged —
`create_window(const WindowDesc&) -> bool` still means `windows.create_primary_window(desc)` — plus
two new methods for secondary windows: `open_window(const WindowDesc&) -> std::optional<WindowId>`
and `close_window(WindowId)`, thin wrappers over `WindowManager::create_window`/`destroy_window`.
`commands()`/`canvas()`/`commands_ptr()`/`canvas_ptr()`/`window_control_ptr()`/`native_window()`/
`drawable_size()` all continue to mean the primary window specifically, now sourced from
`impl_->windows` instead of separate `Impl` members. `tick_loop()` updates the primary's
click-through state, then calls `windows.draw_all()` (in place of the old single `canvas().draw()`)
so every live window is drawn/swapped each frame — secondary windows are simply cleared for now
(their `CommandBuffer` is never populated; that's §21.6). `OpenGLCanvas::draw()` now calls
`SDL_GL_MakeCurrent(window_->window(), context_)` at its very top, before any GL state call — with
2+ live contexts, whichever context happened to still be "current" from initialization order would
otherwise silently receive every window's draw calls (wrong framebuffer, no build/CI signal per
§12.3); this one call is what makes `draw_all()` correct across multiple simultaneously-live
contexts.

### 21.6 Per-window UI and input routing

**`UiCanvas.window`** (`include/engine/ui/canvas.h`, default `kPrimaryWindow`) says which window's
size drives a canvas's `rect` and which window's pointer events can hit-test it.

**Sizing stays split, not unified.** `ui::WindowSize` (a plain `{width, height}`) keeps meaning
exactly what it always meant — "the primary window's drawable size," read from
`world.ctx<WindowSize>()` — with zero changes to its type or to any of the roughly a dozen
pre-existing `world.ctx<engine::ui::WindowSize>().width = …`-style direct writes across
`tests/mvvm_test.cpp`, `tests/render_system_test.cpp`, and `tests/host_test.cpp`; rewriting every
one of those for a purely additive feature would have been churn with no behavior change for any of
them. A new type carries every *other* window's size instead:

```cpp
struct WindowSizes {
    std::unordered_map<WindowId, WindowSize> sizes;   // never holds a kPrimaryWindow entry
};

WindowSize window_size_for(ecs::World& world, WindowId id);   // id == kPrimaryWindow ? ctx<WindowSize>()
                                                                // : ctx<WindowSizes>().sizes[id], default {0,0}
```

`window_size_for` centralizes that branch so `apply_canvas_fit` and `run_ui_render` (below) don't
duplicate it. A `WindowId` with no entry yet in `WindowSizes` (never resized since the window was
created) resolves to `{0, 0}` rather than being an error — matching how a freshly-created window has
no drawable size until its first resize event arrives.

`apply_canvas_fit` (`src/ui/canvas.cpp`) reads `window_size_for(world, canvas.window)` per canvas,
inside its loop, instead of one `ctx<WindowSize>()` read shared by every canvas — so a `FillWindow`
canvas on a secondary window sizes itself from that window, not the primary's.

**Hit-testing.** `MouseEvent` (`include/engine/core/input_system.h`) gains a `window` field
(default `kPrimaryWindow`, first field so the struct's existing designated-initializer construction
sites in `input_system.cpp` need only add `.window = window,`). `InputSystem::handle_mouse_button`/
`handle_mouse_move` each gain a leading `WindowId window` parameter; their one real call site
(`EngineRuntime::poll_events`, below) resolves it from the SDL event. `ui::handle_pointer` gains a
trailing `WindowId window = kPrimaryWindow` parameter (defaulted so every pre-existing call site —
`run_input`'s own logic aside, chiefly the many direct calls across `tests/mvvm_test.cpp` — keeps
compiling unchanged) and now skips any `UiCanvas` whose `window` doesn't match before rect-testing
it, so a canvas assigned to a different window never receives another window's click. `run_input`
(`src/ecs/systems.cpp`) passes `event.window` through on every `MouseEvent::Kind::Down`.

**Render routing.** `EngineSystemDeps` (`include/engine/ecs/systems.h`) keeps its existing
`commands` field meaning exactly what it always meant — the primary window's `CommandBuffer`, used
unchanged by `run_render` and by `run_ui_render` for any `kPrimaryWindow`-targeted canvas — and
gains one new, purely additive field:

```cpp
std::function<render::CommandBuffer*(WindowId)> commands_for_window;
```

Left unset (the default for every existing caller — tests, single-window games), a canvas targeting
a non-primary window is silently skipped for that frame rather than crashing; this is a normal
"that window isn't wired up" case, not a fatal error. `run_ui_render` looks up each canvas's own
`window_size_for` (for its `CmdDrawUI`'s media width/height), and for `window != kPrimaryWindow`
resolves the target buffer via `deps.commands_for_window`, clearing it exactly once per
`run_ui_render` call (tracked with a function-local `std::unordered_set<WindowId>`, so it naturally
resets every invocation) before the first push — mirroring how `run_render` already clears the
primary's buffer once per frame, since a secondary window's buffer has no other system doing that
for it. `Engine<GameT>::init()` (`include/engine/core/engine.h`) wires this to a new
`EngineRuntime::commands_for_window(WindowId) -> render::CommandBuffer*` method, itself a thin
forward to the already-private `WindowManager::commands(id)` — keeping `WindowManager` out of the
public `engine_runtime.h` header while still exposing per-window routing through it.

**`poll_events` demultiplexes by SDL's `windowID`.** A new `WindowManager::find_by_sdl_id(SDL_WindowID) -> std::optional<WindowId>`
(linear scan over live windows — window counts are always tiny, not worth indexing) resolves which
`WindowId` an `SDL_Event` actually belongs to; resolution failure (e.g. a stray event for an
already-closed window) defaults to `kPrimaryWindow`. For `SDL_EVENT_WINDOW_RESIZED`/
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`: a `kPrimaryWindow` resize keeps using the pre-existing
`write_window_size(world, true)` path (`ctx<WindowSize>()`, unchanged); any other window instead
writes `ctx<WindowSizes>().sizes[id]`, sends `WindowResizeEvent{.window = id, …}`, and calls
`ui::apply_canvas_fit(world)` — the same sequence `write_window_size` already ran for the primary,
just addressed at the resized window instead of always assuming primary (previously a latent bug:
before this phase, *any* window resizing re-read the primary's `drawable_size()` regardless of which
OS window actually changed, because only one window could exist). `SDL_EVENT_MOUSE_BUTTON_DOWN`/`_UP`/
`SDL_EVENT_MOUSE_MOTION` resolve the event's window the same way and pass it through to
`InputSystem::handle_mouse_button`/`handle_mouse_move`. `WindowResizeEvent` (`include/engine/ui/canvas.h`)
gains a `window` field (default `kPrimaryWindow`, first field); its two pre-existing construction
sites (`Host::write_window_size`, `EngineRuntime::write_window_size`) used positional aggregate
init, which would have silently misassigned fields once a new one was inserted anywhere but last —
both were converted to designated initializers rather than relying on field order. `Host` has no
multi-window concept and keeps sending `kPrimaryWindow` unconditionally.

Touch and keyboard input stay out of scope for this phase (SDD-noted: touch isn't wired into UI
hit-testing at all yet, and keyboard has no per-window concept to route) — `InputSystem::handle_key`/
`handle_touch`/`handle_touch_move` keep their existing signatures; internally, `handle_touch`/
`handle_touch_move` now pass `kPrimaryWindow` explicitly to the `handle_mouse_button`/
`handle_mouse_move` calls they forward into.

**Per-window UI painting** (the blocker §21.5 named and deferred here) turned out not to need
`OpenGLRenderBackend::execute()` to take a painter parameter at all — see the writeup in §21.5
above for the mechanism (`OpenGLCanvas::draw()` re-arming the shared `ui_painter_` pointer every
frame, right before its own `execute()` call). `WindowManager::create_window` (secondary windows)
now calls `entry->canvas->init(/*with_ui_painter=*/true)` — every window, primary or secondary, gets
its own `NanoVgPainter` — where it previously passed `false` and skipped NanoVG for secondary
windows entirely.

`EngineRuntime::tick_loop` → `WindowManager::draw_all()` still draws/swaps every live window once
per frame (unchanged from §21.5); each window's `draw()` call now also re-arms the shared UI painter
pointer to its own painter and pushes its own `CommandBuffer`'s `CmdDrawUI` commands through it.

**A freshly opened secondary window's `NanoVgPainter` starts with no font loaded — a downstream
bug (`td-over`, the first real-world exercise of this feature), fixed with the same
cache-and-replay shape §21.7 already uses for `WindowSizes`.** Every `OpenGLCanvas` — primary or
secondary — owns its *own* `NanoVgPainter` with its own font atlas (§21.6 above), but
`EngineRuntime::load_ui_font`/`add_font` only ever called
`impl_->windows.canvas_ptr(kPrimaryWindow)->load_ui_font(...)`/`add_font(...)`: nothing loaded any
font into a secondary window's painter, so any `Label`/`Button` targeting one silently failed to
draw text. `EngineRuntime::Impl` (`src/core/engine_runtime.cpp`) now caches every font handed to
those two calls —

```cpp
std::optional<Font> ui_font;              // set by load_ui_font
std::map<AssetId, Font> fonts;            // set by add_font — std::map, not unordered_map:
                                           // AssetId has no std::hash specialization, only
                                           // operator<=>, and this cache is at most a handful of
                                           // entries
std::unordered_set<WindowId> fonts_replayed_for;   // secondary windows already caught up
```

— alongside their existing primary-canvas call, unconditionally (even if the primary call itself
fails). `EngineRuntime::tick_loop()` replays the cache into every secondary window inside the same
`WindowManager::for_each_secondary_window` pass that already backfills `WindowSizes` (§21.7): for
each live window not yet in `fonts_replayed_for`, it looks up the window's canvas via
`impl_->windows.canvas(id)` (the liveness-gated accessor — `nullptr` skips it for this frame,
retried next frame, not marked done), makes that window's GL context current
(`SDL_GL_MakeCurrent(window.window(), canvas->native_context())` — required because this backfill
runs *before* `draw_all()`, so whichever context was left current by the *previous* frame's
`draw_all()` iteration, unordered across windows, is not guaranteed to be this window's own; no
context needs restoring afterward since `OpenGLCanvas::draw()` already makes its own context
current unconditionally at the top of every draw), then calls `canvas->load_ui_font(*ui_font)`
(if set) and `canvas->add_font(font_id, font)` for every cached entry, and marks the window done in
`fonts_replayed_for`. Not covered by a dedicated unit test: `EngineRuntime::Impl` is a private
`.cpp`-only type with no test-reachable accessor for the cache, and the mechanism's actual payoff
(a live secondary `NanoVgPainter` gaining a working font) needs a real GL context, out of
`engine_tests` scope (§12.3) — build + the existing suite (`ctest`) not regressing is the
signal for this phase, consistent with how much of this windowing feature area has already been
verified.

### 21.7 Secondary window lifecycle

**Closing is event-based, not automatic — a deliberate product decision, not an oversight.**
Closing a non-primary window (taskbar close, Alt+F4 on the popup) must not end the game process,
and the engine does not decide unilaterally what a close click means for *any* window, including
the primary one. `EngineRuntime::poll_events` (`src/core/engine_runtime.cpp`) handles
`SDL_EVENT_WINDOW_CLOSE_REQUESTED` by resolving `event.window.windowID` to a `WindowId` via
`WindowManager::find_by_sdl_id` (defaulting to `kPrimaryWindow` on a failed lookup, the same
defensive idiom the resize/mouse cases already use) and sending

```cpp
struct WindowCloseRequestedEvent {   // include/engine/ui/canvas.h, next to WindowResizeEvent
    WindowId window = kPrimaryWindow;
};
```

as `ecs::EventWriter<ui::WindowCloseRequestedEvent>{world}.send({.window = id})` — nothing else.
No `ApplicationState::quit()`, no `WindowManager::destroy_window()`, no interception/cancellation
mechanism of any kind. A game system reads the event in its own `Schedule::Frame`/`Phase::Game`
system and decides: quit, show a confirm dialog, ignore it, or call
`IWindowControl::close_window(id)` for a secondary window. This mirrors how `IGame::on_quit()`
already works — a notification, not a veto point — and keeps that property consistent across the
whole engine rather than adding a one-off exception for windows.

**`IWindowControl::open_window`/`close_window`** (§21.3) are the DI-facing bridge a game actually
calls to invoke this: `open_window(const WindowDesc&) -> std::optional<WindowId>` forwards to
`WindowManager::create_window`, `close_window(WindowId)` to `WindowManager::destroy_window`.
Before this phase these existed only on `EngineRuntime` itself (`open_window`/`close_window`,
§21.5), unreachable from a game's DI graph — `IWindowControl` was the only window-control surface
a game could depend on, and it had no `WindowId`-addressed methods at all.

**A freshly opened secondary window needs an initial `WindowSizes` entry, or its canvases start at
`{0,0,0,0}`.** A `UiCanvas` with `fit = FillWindow`/`ScaleWithScreenSize` targeting a window sizes
itself from `ui::window_size_for(world, canvas.window)` (§21.6), which resolves to `WindowSize{}`
until that window has an entry in `world.ctx<ui::WindowSizes>()` — normally written only by an
actual `SDL_EVENT_WINDOW_RESIZED`/`PIXEL_SIZE_CHANGED` event, not guaranteed to fire immediately (or
at all) right after creation. `IWindowControl::open_window` has no `ecs::World&` to write that entry
itself — `WindowControlImpl`/`WindowManager` deliberately stay ECS-free (rendering/OS layer, §3.4/
§4.2), so threading a `World&` through them was rejected as a layering violation rather than
plumbed through. Instead, `EngineRuntime::tick_loop()` — which already has both `impl_->windows` and
`world` in scope — backfills once per frame, right after `poll_events(...)` and before
`ui::begin_frame(world)`: for every live window other than `kPrimaryWindow` with **no** entry yet in
`WindowSizes`, it writes one from that window's current `drawable_size()` and calls
`ui::apply_canvas_fit(world)` if at least one was filled in. `WindowManager::for_each_secondary_window`
(`src/render/opengl/window_manager.h`/`.cpp`) — a thin enumeration helper mirroring `draw_all()`'s
liveness check (skip `kPrimaryWindow`, skip any entry whose `window.window() == nullptr`) — is what
`tick_loop` iterates. This only ever fills in *missing* entries; a `WindowId` a real resize event
already wrote is never touched by it.

**Borderless window dragging.** Borderless windows have no OS-drawn titlebar to drag by.
`WindowSystem::create()` (`src/render/opengl/window_system.cpp`) installs an
`SDL_SetWindowHitTest` callback **unconditionally, on every window** — not just borderless ones,
because a bordered window's OS titlebar already handles dragging on its own and an
installed-but-inert callback (it only ever returns anything but `SDL_HITTEST_NORMAL` inside a
region the game explicitly set) is harmless there. This also means the callback is installed
exactly once, at `create()` time, and never needs reinstalling: it reads `WindowSystem`'s
`std::optional<render::Rect> drag_region_` member live on every hit-test query, so

```cpp
void WindowSystem::set_drag_region(std::optional<render::Rect> region);   // src/render/opengl/window_system.h
```

just updates that stored value. The callback itself,

```cpp
SDL_HitTestResult window_drag_hit_test(SDL_Window* window, const SDL_Point* area, void* data);
```

(matching `SDL_HitTest`'s exact signature, `src/render/opengl/window_system.h`/`.cpp`) casts `data`
back to the owning `WindowSystem*` and returns `SDL_HITTEST_DRAGGABLE` if `drag_region_` is set and
contains `area->x`/`area->y` (window-client pixels — the same coordinate space `render::Rect`
already uses for `UiCanvas.rect`, e.g. a title-bar canvas's rect), else `SDL_HITTEST_NORMAL`.
`drag_region_` is reset in `destroy()` alongside `transparent_`/`click_through_applied_`.
`IWindowControl::set_drag_region(std::optional<render::Rect>)` (primary-window-only, same scope as
the interface's other style methods — not generalized to `WindowId`, per §21.3) forwards to
`windows_->primary_window().set_drag_region(region)`.

### 21.8 Testing

Same split as every other feature in this SDD (§12.2/§12.3): the platform calls
(`SDL_CreateWindow`, `SetWindowLongPtr`, `SDL_SetWindowHitTest`) are not exercised in
`engine_tests` — no display in CI, and §12.3 already excludes booting a real window. What's pure
logic and gets a `tests/windowing_test.cpp` case:

- `WindowStyle → SDL window-creation flag bitmask` (borderless/always-on-top/transparent, alone
  and combined).
- `should_be_click_through(...)` (§21.4) truth table, including "transparent but click-through
  disabled" and "opaque window" never returning true.
- `CommandBuffer` selection by a `Renderable`/`UiCanvas`'s `window` field — commands for window B
  never land in window A's buffer, and clearing one window's buffer does not touch another's.
- `WindowSizes` (§4.7): a resize event for one `WindowId` updates only that entry; `FillWindow`
  canvases targeting other windows keep their own window's rect.
- `UiInputSystem` hit-testing already covers per-canvas hit-testing (§12.2); the new case is that
  a canvas on window B is never hit by window A's pointer position, using two synthetic pointer
  positions and two `UiCanvas::window` values, no real SDL window needed.

