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

- Hot reload, multi-window.
- 3D spatial audio, Doppler, HRTF.
- JSON/ScriptableObject sound banks in C++ (`GameSounds { … }` with hardcoded volume). Volume/pitch live in audio `.meta`.
- Pitch re-roll every looping-SFX cycle (Lumenwake `LateUpdate` trick) — API may appear later.
- Sharing one process between multiple games.
- Building or mutating visual trees from game C++ as the supported UI API (tests may construct trees).
- Transform parenting, scene-graph matrices, or auto Y-sort unless a later `sort_mode` is added.



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
| Window    | `core/window_system.h`     | SDL window + GL context                            |
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

`WindowSize` in `World::ctx` is the drawable size in pixels (SDL). On `SDL_EVENT_WINDOW_RESIZED` (and at `on_start`): write ctx, `EventWriter<WindowResizeEvent>{ w, h }`.

- `UiCanvas::fit = FillWindow` → engine sets `rect = {0,0,w,h}` before `Input`.
- `UiCanvas::fit = Fixed` → game owns `rect` (centered pause panel, world-space HUD).
- `UiCanvas::fit = ScaleWithScreenSize` → engine writes `rect` to the letterboxed, aspect-preserving real-pixel box for `reference_size` before `Input` (same timing as `FillWindow`); layout/paint/hit-test then run in `reference_size` design units through that box (§8.1).
- `Camera::auto_aspect = true` (default on the active camera) → rebuild ortho from window size; `screen_to_world` / `WorldToScreen` use that camera + `WindowSize`.
- Active camera: `ctx<ActiveCamera>() = Entity`. Exactly one; missing camera is fatal on first `Render`.
- `ctx<WindowSize>` is written **before** `on_start` so `FillWindow` canvases spawned there get a real rect.

Default clear color remains black until a later `Camera::clear` field exists.

---



## 5. Game contract (`IGame`)

```cpp
class IGame {
public:
    virtual ~IGame() = default;
    virtual std::string window_title() const { return "Game"; }
    virtual glm::ivec2 window_size() const { return {800, 600}; }
    virtual ecs::World& world() = 0;
    virtual void on_start() = 0;
    virtual void on_fixed_update() = 0;  // Schedule::Fixed, 0..N times
    virtual void on_update() = 0;       // Schedule::Frame, once
    virtual void on_draw() = 0;
    virtual void on_quit() = 0;
};
```

Title/size are **not** hardcoded inside `Engine::init`. The host constructs `IGame` from the injector, then `WindowSystem::create(game->window_title(), game->window_size())`. `World` exists after `Game` construction. Host calls `register_engine_systems` then `on_start` (scene spawn, `add_system` Game phase).

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

`ENGINE_BUILD_TESTS` defaults to **ON** when this repo is the CMake root, **OFF** when a game does `add_subdirectory(external/engine)` — so games do not compile gtest unless they pass `-DENGINE_BUILD_TESTS=ON`.

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
2. `IGame::window_title` / `WindowSize`; `Engine` uses them instead of a hardcoded title/size.
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
18. Splash screen is one `UiCanvas`/`Image` entity reusing the existing CSS `@keyframes` opacity animator, not a new draw path or a `Host::tick` pause — on by default with a builtin asset (§20).

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

### 20.1 `IGame` contract

```cpp
struct SplashScreen {
    bool enabled = true;
    AssetId image = builtin::splash_wind;
    float fade_in_seconds = 0.4f;
    float hold_seconds = 1.0f;
    float fade_out_seconds = 0.4f;
};

virtual SplashScreen splash_screen() const { return {}; }
```

Same shape as `window_title()` / `window_size()` / `window_icon()` (§5, §19.1): a virtual with a
sensible default, no separate config file. A game overrides `image` for its own splash, tunes the
three durations, or sets `enabled = false` to skip it outright. No single `duration_seconds` —
three phases, because the animation (§20.3) needs fade-in and fade-out timed independently from
the hold in the middle.

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
moved — both stay exactly as they are today. Note: `include/engine/core/host.h`'s `Host` class
*looks* like the per-frame entry point (and is what earlier drafts of this section assumed), but
it is not actually wired into `Engine<GameT>::run()` — the real production loop is
`EngineRuntime::begin_loop()` (one-time: `game.on_start()`, `ui::apply_canvas_fit()`, starts
`running`) plus `EngineRuntime::tick_loop()` (per-frame: fixed/frame update, `canvas().draw()`) in
`src/core/engine_runtime.cpp`. `Host` is a separate, parallel class exercised only by
`tests/host_test.cpp`; changes here belong in `EngineRuntime`, not `Host` (though mirroring the
change there too, if cheap, keeps the two from drifting further apart — not required). The splash
is not a pause-like blocking phase; it is one more `UiCanvas`/`UiInstance` entity drawn on top of
everything else, fading itself out via machinery that already exists:

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
- Spawned once — gated on `splash_screen().enabled` — as a `UiCanvas{fit = UiFit::FillWindow,
  order = <high, above every other canvas>}` + `UiInstance{document, stylesheet}` entity, most
  naturally right where `EngineRuntime::begin_loop()` already calls
  `ui::apply_canvas_fit(game.world())` (that's after `Engine::init()`'s catalog/image-preload
  loop has already finished, so the builtin/game splash image is already resolved through
  `AssetsDb` by the time this entity exists). `world.create()` + `world.emplace<ui::UiCanvas>(...)`
  + `world.emplace<ui::UiInstance>(...)` is the existing spawn pattern — see
  `spawn_button_canvas` in `tests/mvvm_test.cpp` for a working example of building a `UiCanvas` +
  `UiInstance{parsed_document}` pair from a `parse_xml` result.
  `run_ui_render` (`src/ecs/systems.cpp`, the existing `Phase::UiRender` system) already walks
  every `UiCanvas`/`UiInstance` entity and pushes its draw calls through the same `CommandBuffer`
  → render-backend path everything else uses — no `ICanvas`/`OpenGLCanvas` changes needed.
- `element.animation_elapsed` clamps at `animation_duration` and the animation's last keyframe
  stop is `opacity: 0`, so once the fade-out finishes the element simply sits invisible forever —
  correct output with no explicit "done" transition needed. Despawning the now-inert entity
  afterward is a cheap tidiness improvement, not required for correctness; leave it to whoever
  implements this to decide if it's worth a small system versus one permanently-idle entity.
- No canvas-clear-color change needed: `OpenGLCanvas::draw()` already clears to black
  (`glClearColor(0,0,0,1)`) every frame regardless, so a fully faded-out splash already reveals
  black underneath with no special-casing.

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
are a one-line contract test, same pattern as `IGame::window_icon()`'s `nullopt` default (§19.5) —
already done (§20.1). Whether the spawned `UiCanvas`/`UiInstance` entity actually renders on top
of everything else is not tested — GPU/window excluded from `engine_tests` (§12.3), same as
everything else in §19 and §18 — but that the entity gets spawned at all when `enabled` and *not*
spawned when disabled is an ECS-level check (`world.view<ui::UiCanvas>()` count), no window
needed, same spirit as `tests/host_test.cpp`'s existing `Host` construction tests.

