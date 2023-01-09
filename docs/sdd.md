# Engine Software Design Document


| Field     | Value                                                                                |
| --------- | ------------------------------------------------------------------------------------ |
| Document  | SDD-WIND-001                                                                         |
| Project   | **Wind** — 2D game engine for remakes                                                |
| Status    | Design of record for the **target** standalone repo                                  |
| Baseline  | Extracted from `_ref_ping-pong` (`github.com/wismh/ping-pong`), then adapted         |
| Language  | C++23                                                                                |
| Tests     | GoogleTest, target `engine_tests` (see §12)                                          |
| Consumers | Sibling game repos (`tic-tac-toe/`, later `pong/`, `arkanoid/`, …) via git submodule |


This document describes **what the engine is designed to be** after extraction. Ping-pong is the working prototype; where this file disagrees with ping-pong source, **this file wins** for the new repo. Implementation notes that are still open live in §17.

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

- Gameplay, levels, AI, menus of a specific remake.
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
| `TryGet` / `Get`| Optional vs fatal asset lookup (see §10.7)                                         |
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
4. **Named input.** Gameplay binds scancodes → action strings, not raw keys in systems.
5. **Assets only by GUID.** `AssetsDb::Get<T>(AssetId)` (fatal if missing cooked asset) or `TryGet`. No filenames in game code.
6. **Import settings live in `.meta`.** A bare PNG/WAV is not a texture/sound until its sidecar says how to load it (color space, filter, sound bank, …).
7. **Audio is a system, not a filename firehose.** `IAudioSystem` plays `Sound` objects produced by the audio importer, not `PlaySoundEvent{"hit.wav"}`.
8. **Reusable across remakes.** Window title/size come from `IGame`; audio and render APIs stay game-agnostic.
9. **Test the engine, not the remakes.** Logic that will be shared (ECS, events, commands, audio policy, meta/catalog, input, camera, fixed-step loop) has GoogleTest coverage in this repo. Gameplay stays in the game repo.
10. **Simulation is fixed-step.** Frame time drives present and audio fades; gameplay/physics tick at a constant `fixedDeltaTime` (§4.4).
11. **UI is markup + style + VM.** Games do not build `UIElement` trees in C++. XML + custom CSS + `ViewModel` / `ICommand` (see §8).
12. **Draw with materials, then sort.** `Renderable` is mesh + material + layer, not ad-hoc shader/texture pointers with undefined order (§6).



### 2.2 Non-goals (v1)

- Hot reload, multi-window, mobile / Emscripten.
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
  docs/slices.md          # implementation order + test gates
  include/engine/         # public API only (games may include these)
  src/                    # .cpp + private headers (not on the game include path)
  tools/asset_guid/       # write missing .meta + GUIDs (dev only)
  tools/asset_codegen/    # read-only: asset_ids.h + cooked catalog
  tests/                  # GoogleTest sources (no game code)
  builtin_assets/         # default shader/mesh/material/font; committed .meta; well-known GUIDs
  external/               # git submodules (SDL3, glm, glad, spdlog, boost_di, nanovg, tinyxml2, SDL_mixer, googletest, tomlplusplus)
```

Engine CMake **owns** third-party targets (including nanovg). Include paths use `${CMAKE_CURRENT_SOURCE_DIR}/external/…`, not `../external/` as in ping-pong.

### 3.2 Game repo

```
tic-tac-toe/
  engine/                 # git submodule, url = ../engine (local); GitHub URL later
  CMakeLists.txt
  include/game/ …
  src/ …
  assets/                 # raw files + sidecar .meta; copied next to the exe
  generated/              # asset_ids.h + catalog (CMake output; gitignore the binaries, commit ids if desired)
```

```cmake
add_subdirectory(engine)
# asset_codegen (read-only) runs on assets/ before compiling game sources
# missing .meta → configure/build FAIL; run asset_guid locally, commit the new .meta
add_executable(tic-tac-toe … ${GENERATED_CATALOG})
target_link_libraries(tic-tac-toe PRIVATE engine)
target_include_directories(tic-tac-toe PRIVATE ${CMAKE_BINARY_DIR}/generated)
```

After clone: `git submodule update --init --recursive` (engine’s own `external/` submodules included). Game CMake does not build `engine_tests` unless `-DENGINE_BUILD_TESTS=ON`.

### 3.3 Why a separate repo

Ping-pong nested `engine/` under a game and put SDL next to it. That cannot be a submodule. Extracting the engine makes every remake a sibling that pins a commit of `engine`.

### 3.4 Public headers vs private implementation

Two include roots. CMake:

```cmake
target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

A game `target_link_libraries(… PRIVATE engine)` therefore sees **only** `include/`. It cannot `#include` a file that lives under `src/`. There is **no** `include/engine/detail/` on the public path — putting “please don’t use this” headers under `include/` is not a facade.

**Public (`include/engine/`):** `engine.h` (umbrella), `igame.h`, `log.h` (facade, not spdlog), ECS (`world.h`, `entity.h`, components games spawn), `Time`, `Events<T>` / `EventReader` / `EventWriter`, `Command` / `CommandBuffer`, `ICanvas` / `IGraphicFactory` / `IMesh` / `IShader` / `ITexture` / **`IMaterial`**, `UiCanvas` / `ViewModel` / `Bindable` / `ICommand`, `AssetsDb`, `IAudioSystem` / `Sound`, `IFatalError`, `builtin_ids.h`. Games include these (or the umbrella).

**Private (`src/…`, never installed, never PUBLIC):** OpenGL/glad types, NanoVG context, SDL window/GL bootstrap, mixer tracks, Loop internals, importers, XML/CSS parsers, cooked-catalog parser. `OpenGLCanvas::Draw` executes commands here.

**Forbidden in game code (and not possible if CMake is followed):** `#include <glad/…>`, SDL render/mixer headers, spdlog, NanoVG, tinyxml2, any `src/` engine header, `gl*` / `MIX_*` / `nvg*` calls.

Ping-pong ships almost every `.h` next to the game include path. Extraction **splits** that. `IGame::WindowSize` uses `glm::ivec2` — glm is a **PUBLIC** link of `engine`.

---



## 4. System architecture

```
main
  → Engine<GameT>::Init()     Boost.DI + SDL + window + GL + UI canvas + audio
  → Engine::Run()
       → Loop::Run()
            OnStart
            while running:
              World::FlushEvents()              // age Events<T> (start of frame)
              frameDt = clamp(realDt, 0, 0.25)
              PollEvents (QUIT, resize, InputSystem → queues)
              ctx<WindowSize>, WindowResizeEvent if size changed
              ctx<MouseConsumed> = false
              IAudioSystem::Update(frameDt)     // fades use wall-clock (also while paused)
              if not ApplicationState.paused:
                accumulator += frameDt
                steps = 0
                while accumulator >= FIXED && steps < MAX_FIXED_STEPS:
                  Time.fixedDeltaTime = FIXED
                  IGame::OnFixedUpdate()        // World::Run(Schedule::Fixed)
                  accumulator -= FIXED
                  steps += 1
              Time.deltaTime = frameDt
              IGame::OnUpdate()                 // World::Run(Schedule::Frame)
              ICanvas::Draw()
            OnQuit
```



### 4.1 Logical components


| Area      | Path                       | Responsibility                                     |
| --------- | -------------------------- | -------------------------------------------------- |
| Host      | `core/engine.h`            | DI graph, SDL init, window title/size from `IGame` |
| Time      | `core/time.h`              | `deltaTime` (frame), `fixedDeltaTime`, accumulator |
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
| Codegen   | `tools/asset_guid`, `asset_codegen` | Write GUIDs vs read-only generate            |
| Tests     | `tests/`                   | GoogleTest (`engine_tests`, see §12)               |




### 4.2 Dependency injection and systems

`Engine<GameT>::Init` builds a Boost.DI injector. Singletons include `ApplicationState`, `Time`, `AssetsDb`, `InputSystem`, `IFatalError`, `CommandBuffer`, `ICanvas` → `OpenGLCanvas`, `IGraphicFactory` → `OpenGLFactory`, `IRenderBackend` → `OpenGLRenderBackend`, `IAudioSystem` → `AudioSystem`, `IGame` → `GameT`.

`IGame` is constructed by the injector (constructor parameters = services). Systems are **not** resolved from DI inside `Update()`. They capture `shared_ptr` services when constructed in `OnStart`, or they read `World::ctx<T>()` (`Time`, `WindowSize`, `MouseConsumed`, `ActiveCamera`, `Events<U>`).

`engine::RegisterEngineSystems(world, …)` is called by the **host** after `World` exists and **before** `OnStart`. Games only `AddSystem` into **`Phase::Game`**.

Do not introduce a service locator (`Engine::GetAudio()`).

Games receive services through `Game`’s constructor. Systems write/read typed events (`EventWriter` / `EventReader`); they must not load files or pass paths.

### 4.3 One world, no Node graph

Ping-pong’s `Node` / `NodeEcs` / `NodeUI` is **removed**. There is no scene-graph type beside ECS.

`IGame` owns `ecs::World`. `OnStart` registers **game** systems onto `Schedule::Fixed` or `Schedule::Frame` at `Phase::Game`. `OnFixedUpdate` / `OnUpdate` run those schedules (see §4.4–§4.5).

```
ecs::World
  entity Camera     + Camera (ortho, auto_aspect) + optional Transform
  entity HUD        + UiCanvas { document, stylesheet, data_context, fit = FillWindow }
  entity PauseMenu  + UiCanvas { …, order = 1 }   // optional
  entity Player     + Transform + Renderable + …
```

Widget buttons are **not** ECS entities. The visual tree is the **instance** of an XML document under `UiCanvas` (WPF visual tree vs view-model). Layout (flex, gap) lives in markup + CSS, not in the registry.

World-space labels later: same `UiCanvas`, `rect` written each frame from `Transform` + `Camera::WorldToScreen` (`fit = Fixed`). No second graph.

Mouse: `UiInputSystem` (phase `Input`) hit-tests canvases **front-to-back** (`UiCanvas::order`). On hit, `ctx<MouseConsumed>() = true` and the bound `ICommand` runs (see §8). Gameplay click systems in `Phase::Game` must respect `MouseConsumed`.

There is **no** `Transform` parent. A turret that must follow a tank is a game concern in v1 (copy position in a system) until a `Parent` component exists.

### 4.4 Fixed timestep

Ping-pong (and the first draft of this SDD) passed a **clamped variable `dt`** into every system. That makes physics and turn timing frame-rate dependent. This repo does **not** do that.

Constants (in `Time` / Loop):

| Name | Value | Role |
| --- | --- | --- |
| `FIXED` | `1/60` s | one simulation tick |
| `MAX_FIXED_STEPS` | `8` | spiral-of-death cap (hitch → at most 8 ticks, then drop remainder) |
| `frameDt` clamp | `0.25` s | ignore a huge stall as one giant frame |

`Time` fields:

- `deltaTime` — this **frame’s** clamped wall time (UI animation, audio fades already ticked with `frameDt` in Loop).
- `fixedDeltaTime` — always `FIXED` inside `OnFixedUpdate`.
- `alpha` — `accumulator / FIXED` after the sim loop (0..1). Reserved for interpolating renderables later; v1 may ignore it.

**Which schedule:**

| Schedule | When | Put here |
| --- | --- | --- |
| `Fixed` | 0..N times per frame, dt = `FIXED`; **skipped while paused** | physics integrate, collision probe, movement, anything that must be fps-independent |
| `Frame` | once per frame, dt = `deltaTime`; **always runs** (pause menus, UI) | input, bindings, render, UI, click-to-cell / other **one-shot input** gameplay |

Input is polled **once per frame** before the `while`. A click is visible to Frame systems **this same frame** (§9). If a click system ran on **Fixed**, two sim steps in one frame could apply the same click twice. **One-shot input gameplay runs on Frame, `Phase::Game`.** Held keys (state map) are fine to read from Fixed.

`IGame::OnFixedUpdate` → `world.Run(Schedule::Fixed)`. `IGame::OnUpdate` → `world.Run(Schedule::Frame)`. Do not call `world.Run` for both schedules from a single hook.

Tests: given `accumulator` math (or a testable `FixedStepClock`), `dt = 1/60` → 1 step; `dt = 2/60` → 2 steps; `dt = 9 * FIXED` → exactly `MAX_FIXED_STEPS` and leftover discarded. While `paused`, zero Fixed steps and accumulator does not grow.

### 4.5 Phases (order inside a schedule)

Registration order inside a **phase** is execution order. Games do not pick a raw index among engine systems. They only add `Phase::Game`.

**`Schedule::Fixed`**

| Phase | Who | Does |
| --- | --- | --- |
| `Physics` | engine | integrate, AABB probe, `CollisionEvent` |
| `Game` | remake | movement responses, gameplay that must be fps-independent |

**`Schedule::Frame`**

| Phase | Who | Does |
| --- | --- | --- |
| `Input` | engine | `UiInputSystem` (hit-test, `ICommand::Execute`, `MouseConsumed`) |
| `Game` | remake | world picking if not consumed; mutate ViewModels; `EventWriter` |
| `Bind` | engine | push `Bindable<T>` / commands into the XML instance tree |
| `Audio` | engine | `EventReader<PlaySfxEvent>` / music — **after** Game so same-frame SFX work |
| `Render` | engine | sort `Renderable`s, push `CmdDrawMesh` |
| `UiRender` | engine | push `CmdDrawUI` (so HUD is on top of the world) |

`AddSystem(Schedule, Phase::Game, system)` is the game API. Engine phases are registered by `RegisterEngineSystems`.

### 4.6 Pause

`ApplicationState::paused` (bool). Loop: **do not** run Fixed, **do not** add to `accumulator` (unpause must not dump 8 sim steps). Frame still runs so a pause `UiCanvas` can bind Continue/Quit.

Audio `Update(frameDt)` still runs (music keeps fading unless the game `StopMusic`). Gameplay SFX from skipped Fixed systems simply do not fire.

### 4.7 Window resize and camera

`WindowSize` in `World::ctx` is the drawable size in pixels (SDL). On `SDL_EVENT_WINDOW_RESIZED` (and at `OnStart`): write ctx, `EventWriter<WindowResizeEvent>{ w, h }`.

- `UiCanvas::fit = FillWindow` → engine sets `rect = {0,0,w,h}` before `Input`.
- `UiCanvas::fit = Fixed` → game owns `rect` (centered pause panel, world-space HUD).
- `Camera::auto_aspect = true` (default on the active camera) → rebuild ortho from window size; `ScreenToWorld` / `WorldToScreen` use that camera + `WindowSize`.
- Active camera: `ctx<ActiveCamera>() = Entity`. Exactly one; missing camera is fatal on first `Render`.
- `ctx<WindowSize>` is written **before** `OnStart` so `FillWindow` canvases spawned there get a real rect.

Default clear color remains black until a later `Camera::clear` field exists.

---



## 5. Game contract (`IGame`)

```cpp
class IGame {
public:
    virtual ~IGame() = default;
    virtual std::string WindowTitle() const { return "Game"; }
    virtual glm::ivec2 WindowSize() const { return {800, 600}; }
    virtual ecs::World& World() = 0;
    virtual void OnStart() = 0;
    virtual void OnFixedUpdate() = 0;  // Schedule::Fixed, 0..N times
    virtual void OnUpdate() = 0;       // Schedule::Frame, once
    virtual void OnDraw() = 0;
    virtual void OnQuit() = 0;
};
```

Delta vs ping-pong: title/size are **not** hardcoded `"Ping Pong"` / `{800,600}` inside `Engine::Init`. The host constructs `IGame` from the injector, then `WindowSystem::Create(game->WindowTitle(), game->WindowSize())`. `World` exists after `Game` construction. Host calls `RegisterEngineSystems` then `OnStart` (scene spawn, `AddSystem` Game phase).

`OnDraw` stays empty: world draw is `Phase::Render`; UI is `Phase::UiRender`; present is `OpenGLCanvas::Draw`. Do not push commands from `OnDraw`.

---



## 6. Rendering

Ping-pong `Renderable` is `{ mesh, shader, texture }` with **undefined draw order** and blend hardcoded to `(src alpha, one)`. This engine uses **materials** and an explicit **sort key**.

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

**There is no `CmdCustomDraw`.** Ping-pong’s `std::function<void()>` escape hatch is deleted. Extra draw paths = new **named** command types in the engine (public header + private execute).

`CommandBuffer` is a FIFO. **Sort happens in `RenderSystem` before push**, not inside execute. `UiRender` runs after `Render`, so HUD commands follow world commands. Clear the buffer at the start of `Phase::Render` so Fixed systems never accumulate draws.

`OpenGLCanvas::Draw`: clear → `CommandBuffer::Execute` → `SDL_GL_SwapWindow`. Execute lives in `src/`.

### 6.2 Materials

A material is a cooked asset (`importer = "material"`), not three loose pointers on `Renderable`.

```cpp
enum class BlendMode { Opaque, Alpha, Additive }; // Additive = ping-pong (src α, one)

class IMaterial {
public:
    virtual ~IMaterial() = default;
    virtual std::shared_ptr<IShader> Shader() const = 0;
    virtual std::shared_ptr<ITexture> Texture(int slot) const = 0; // 0 = albedo
    virtual glm::vec4 Color() const = 0;
    virtual BlendMode Blend() const = 0;
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

`AssetsDb::Get<IMaterial>(id)`. Games may multiply instance color on `Renderable`; they do not set GL blend in C++.

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

Game code depends on interfaces and `Get<IMaterial>` / `Get<ITexture>`, not glad or paths.

### 6.5 Camera and coordinates

Orthographic **Camera** component. `RenderSystem` uses `ctx<ActiveCamera>()`. `ScreenToWorld` / `WorldToScreen` take that camera + `WindowSize`.

| Space | Origin | Y |
| --- | --- | --- |
| SDL mouse / `UiCanvas.rect` | top-left of window | **down** |
| World / `Transform` | game-defined; default camera looks at origin | **up** |
| NanoVG inside a canvas | top-left of `rect` | **down** |

Default shader (builtin): GLSL 330, `uModel/uView/uProjection`, `uTexture`, `uColor`. Mesh format (`.mesh`): `pos.x pos.y pos.z uv.x uv.y` per vertex, `#` comments.

### 6.6 Blend (execute)

Set from `IMaterial::Blend()` per `CmdDrawMesh`. UI is NanoVG in a later command (its own blend). Do not inherit ping-pong’s global `(src alpha, one)` for every sprite.

---



## 7. ECS (EnTT as reference, not a dependency)

Do not vendor EnTT. Implement `ecs::World` **in this repo**, using EnTT as the **API and implementation reference** (generational index, sparse-set / packed storage, `view`, `try_get`, destroy that bumps generation).

Contract the homemade registry must keep (names may match EnTT so games feel familiar):

- `Entity` = index **plus generation**. Recycled ids do not alias live entities.
- `emplace` / `get` / `try_get` / `remove` / `destroy`.
- `view<T, U>()` — iteration over packed data, not `typeid().name()` string keys.
- Do not invalidate a view you are iterating; defer `destroy` if a system needs it (command buffer / `destroy` queue flushed after the view).
- `World::ctx<T>()` for singletons: `Time`, `WindowSize`, `MouseConsumed`, `ActiveCamera`, `Events<U>` (first access **registers** `U` for `FlushEvents`).
- Engine systems are registered by `RegisterEngineSystems` into the phases in §4.5. They take `World&` plus constructor-injected `shared_ptr` services (`CommandBuffer`, `AssetsDb`, …).
- Game systems: `world.AddSystem(Schedule::Fixed | Frame, Phase::Game, …)` in `OnStart` only.

Ping-pong pools (erase-from-vector without fixing indices, `uint32_t` without generation) are **not** copied.

**Engine components:** `Transform` (no parent), `Renderable` (§6.3), `Camera`, `RigidBody`, `BoxCollider`, `UiCanvas` (§8).

**Physics:** AABB + velocity is a **collision probe**, not a solver. Writes `CollisionEvent` to `Events<CollisionEvent>` on enter. Bounce stays in game systems until a real solver exists.

---



## 8. UI (XML + CSS + MVVM)

NanoVG (GL3) draws the **instance** of a markup document. This is not ping-pong `Layout`/`Label` trees with `onClick` lambdas, and not one ECS entity per widget.

WPF split, mapped to this engine:

| WPF | This engine |
| --- | --- |
| XAML | XML document asset (`importer = "ui"`) |
| ResourceDictionary / Style | custom CSS asset (`importer = "css"`) |
| `DataContext` + `{Binding}` | `ViewModel` + `Bindable<T>` registered by name |
| `ICommand` / `RelayCommand` | `ICommand` / `RelayCommand` |
| code-behind `x:Class` | **none** — no `.cpp` for a view |
| `ControlTemplate` / VSM | **not v1** |

### 8.1 `UiCanvas`

```cpp
enum class UiFit { FillWindow, Fixed };

struct Rect { float x, y, w, h; };  // screen pixels, origin top-left (SDL)

struct UiCanvas {
    AssetId document;                       // .xml
    std::optional<AssetId> stylesheet;      // .css; else xml `stylesheet` attr
    std::shared_ptr<ui::ViewModel> data_context;
    Rect rect{};                            // scissor + layout origin
    UiFit fit = UiFit::FillWindow;
    int order = 0;                          // higher = later draw / hit-test
};
```

Spawn: `emplace<UiCanvas>(hud, { .document = assets::ui::hud, .data_context = hudVm })`. Game code does **not** `make_shared<Layout>()` or set `onClick`.

The runtime tree is owned by the UI module (cached instance per canvas). Reloading XML every frame is forbidden; rebuild when `document` / stylesheet / `DataContext` pointer changes.

Hit-test: mouse minus `rect` origin. Fonts: `Get` + `importer = "font"`; CSS `font-family` names a font **AssetId** (hex) or a builtin name (`default`).

### 8.2 XML (markup)

Parsed with tinyxml2 **in `src/`**. Cooked catalog stores path + importer; runtime parses XML **once** on first `Get<UiDocument>` (not every frame; not TOML).

v1 elements:

| Tag | Role |
| --- | --- |
| `Canvas` | root; optional `stylesheet="32-hex"` |
| `Stack` | ping-pong `Layout`: `direction` horizontal/vertical, `gap`, `align` |
| `Label` | text |
| `Button` | hit-target; `Command` binding |
| `Image` | `Source` = texture/ui_image AssetId or `{Binding}` |
| `ItemsControl` | repeats `ItemTemplate` over `ItemsSource` |

```xml
<Canvas stylesheet="b0a1c2d3e4f5678901234567890abcde">
  <Stack class="hud" direction="vertical">
    <Label class="title" Text="{Binding Title}"/>
    <Label Text="{Binding Score}"/>
    <Button Command="{Binding Restart}" Content="{Binding RestartLabel}"/>
    <ItemsControl ItemsSource="{Binding Cells}">
      <ItemTemplate>
        <Button class="cell" Command="{Binding Click}" Content="{Binding Mark}"/>
      </ItemTemplate>
    </ItemsControl>
  </Stack>
</Canvas>
```

WPF-shaped `{Binding Path}` (path = registered name). `Mode=OneWay` default (VM → view). `Mode=TwoWay` reserved (sliders); not required for tic-tac-toe.

`id` / `class` / `Name` attributes: CSS hooks. `Name` is not FindName-from-game; games do not reach into the tree.

Unknown tags / unknown bind paths: **load-time fatal** (`IFatalError` / codegen warning + runtime fatal on Get), not a silent empty label.

**Forbidden in XML:** filenames, `onClick`, inline GL, script. Asset refs are 32-hex GUIDs (or bindings that yield `AssetId`).

Building the same tree in C++ is allowed **only in `engine_tests`**.

### 8.3 Custom CSS

Not browser CSS. Engine parser in `src/` (no libcss). File + `.meta` `importer = "css"`.

**Selectors (v1):** `Element`, `.class`, `#id`, `Element.class`. **No** descendant/child combinators (`A B`, `A > B`), no `,` grouping beyond listing duplicate rules.

**Pseudos:** `:hover`, `:pressed`, `:disabled` (buttons). No `:nth-child`, no `::before`.

**Units:** unitless number = **pixels**. No `%`, `em`, `vw`, `calc()`, `var()`.

**Properties (v1)** — ignore unknown with a **warn** (do not fail the whole sheet):

`color`, `background`, `opacity`, `visibility`, `width`, `height`, `min-width`, `min-height`, `padding` (1–4), `margin` (1–4), `gap`, `flex-direction`, `align-items`, `justify-content`, `border-radius`, `border-width`, `border-color`, `font-size`, `font-family`.

```css
.hud { padding: 16; gap: 8; flex-direction: vertical; }
.title { font-size: 24; color: #ffffff; }
Button { padding: 8 12; border-radius: 4; }
Button:hover { background: #333333; }
Button:pressed { background: #111111; }
Button:disabled { opacity: 0.5; }
```

Cascade: element < class < id < pseudo. Later file rules win if the xml `stylesheet` is a single sheet. Multiple sheets: `stylesheet` attr is one GUID; extra sheets are a later `UiCanvas` field if needed.

`@import`, `@media`, animations: **not v1**.

### 8.4 MVVM (C++, no reflection)

There is no C++ RTTI binding to arbitrary members. A `ViewModel` **registers** names (WPF property names, handwritten).

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual bool CanExecute() const = 0;
    virtual void Execute() = 0;
};

template<typename T>
class Bindable { /* Set/Get; notifies the binding engine only */ };

class RelayCommand : public ICommand { /* ctor from std::function; CanExecute bindable */ };

class ViewModel {
protected:
    template<typename T>
    void Property(std::string_view name, Bindable<T>&);
    void Command(std::string_view name, ICommand&);
    // ItemsSource: Property("Cells", cells) with BindableList<T>
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
        Property("Title", title);
        Property("Score", score);
        Command("Restart", restart);
        restart = [this] { /* send event or mutate game model — not GL, not UI tree */ };
    }
};
```

**DataContext** on `UiCanvas` is inherited by children (WPF). `ItemsControl` sets the item as DataContext for each cloned `ItemTemplate`. Nested VMs are `Bindable<std::shared_ptr<ViewModel>>` if needed.

**Phase `Bind`:** copy registered values into the instance tree (text, content, `CanExecute` → `:disabled`). One-way, every frame is acceptable in v1 (no dirty-rect requirement). `Bindable::Set` from Fixed is visible next Frame Bind.

**Commands:** `UiInputSystem` on hit calls `ICommand::Execute()` if `CanExecute()`. That is the **only** UI → game path. Execute may `EventWriter::send` or set other `Bindable`s. It must not include glad, touch `UIElement*`, or call `CommandBuffer`.

Ping-pong `std::function<void()> onClick` on `Layout` is **deleted** from the public API.

`INotifyPropertyChanged` is not a game-facing interface; `Bindable<T>` is the notification.

### 8.5 Input vs world

`MouseConsumed` is set **false** at the start of the Loop body, then `UiInputSystem` may set true. World picking in `Phase::Game` reads it. Do not reset it at end of frame.

---



## 9. Input and events (Bevy queues)

Ping-pong `EventBus` (`Subscribe` + `Emit`, no unsubscribe) is **removed**. Immediate observer lists dangle when a node dies and re-enter unsafely during `Emit`. UI clicks are **not** a second bus: they are `ICommand` (§8.4).

Replace with **double-buffered queues**, same shape as Bevy `Events<T>` / `EventReader` / `EventWriter`.

**Storage:** `Events<T>` lives in `World::ctx<Events<T>>()`. First access **registers** `T` on a type-erased list. `World::FlushEvents()` (start of Loop) calls `update()` on every registered type. There is **no** central `EventQueues` object that must know game event types at compile time of the engine.

**Lifetime (Bevy-like, two frames):**

- `FlushEvents` at **start** of the iteration ages buffers (drop events older than one extra frame).
- `EventWriter<T>::send` appends to the **current** buffer.
- `EventReader<T>` iterates **previous + current** (events sent earlier this frame are visible to later phases). Input polled after Flush is therefore visible to `Phase::Input` **the same frame**. `PlaySfxEvent` sent in `Phase::Game` is visible to `Phase::Audio` the same frame.
- Events do not live forever.
- No `Unsubscribe`: readers hold no allocation in the queue; writers do not store `this`.

Input:

- `InputSystem` (SDL poll in Loop, before schedules) writes `InputEvent` / `MouseEvent` / held-state map. It does not call UI.
- Bindings: scancode → action string (codegen/enum can come later; strings stay until then).
- Key **down / up / held** must be represented (held = state map updated from down/up, not a one-shot event only).

`PlaySfx` from gameplay: `EventWriter<PlaySfxEvent>` (preferred) so `Phase::Audio` plays it. Direct `IAudioSystem` from a game system is allowed. Still no filenames.

---



## 10. Assets

Ping-pong loads by filename (`Get<ITexture>("ball.png")`). That breaks when files move and puts import policy in C++. This engine loads **only by GUID**. The raw bytes + a sidecar `.meta` are the source of truth; C++ sees generated constants.

### 10.1 `AssetId`

Strong type wrapping a **32-character lowercase hex** GUID (same length/charset as Unity `.meta`). Invalid length/charset is a load error. GUIDs are unique in a game’s `assets/` tree and **never change** after the asset is referenced from code or other metas.

```cpp
db.Get<ITexture>(assets::textures::player);      // T; missing cooked → fatal
db.TryGet<Sound>(assets::sfx::step);             // Result; caller handles NotFound
```

Forbidden: `Get<T>("player.png")`, concatenating `ASSETS_PATH` in game code, dereferencing a null `Get`.

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

`asset_codegen` output: `${CMAKE_BINARY_DIR}/generated/`. Gitignore generated files. **Commit `.meta`.** CI must not run `asset_guid`.

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

`TryGet` / `Get` use the catalog, then load raw bytes from `ASSETS_PATH + relativePath` with the cooked importer fields. Cache by `(AssetId, T)`.

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

v1 may ship `layout = "single"` only. The TOML schema includes `[[sprites]]` so atlas remakes do not change GUIDs later. `Get<ITexture>` returns the atlas; sprite rects from cooked meta (`GetSprite(id, "idle")` when implemented).

### 10.7 `Get` vs `TryGet`

`Result` with only `{ T, None }` hides **corrupt vs missing**. Use an error enum.

```cpp
enum class AssetError {
    NotFound,
    Corrupt,
    TypeMismatch,
    NotReady        // Get before GL/audio init
};

std::expected<std::shared_ptr<T>, AssetError> TryGet(AssetId);

std::shared_ptr<T> Get(AssetId);  // never null
```

- **`TryGet`:** caller must handle `AssetError`. `NotFound` is valid for optional content. `Corrupt` / `TypeMismatch` should usually still be treated as fatal by the game, but the API does not hide them as `None`.
- **`Get`:** `TryGet` + on any error call `IFatalError` (message includes GUID + error) and **do not return**. Game hook: system dialog (e.g. `SDL_ShowSimpleMessageBox`) + `ApplicationState::Quit()` / abort so the process does not continue with a missing cooked asset. Test hook: `ADD_FAILURE` / fail the test — **no dialog**.

`Get` is the default in gameplay. `TryGet` is for content that may be absent (mod slot, optional pack). Examples in this SDD must not dereference a pointer that can be null.

`IFatalError` is injected; `AssetsDb` does not hardcode Win32/`MessageBox`.

### 10.8 Engine builtin assets

Default shader / unit quad / unlit sprite material / UI font are **not** copied by hand into every remake.

- Source: `engine/builtin_assets/` with committed `.meta` and **stable well-known GUIDs**.
- CMake copies them to `<exe dir>/assets/engine/` (engine tests and games).
- Runtime loads a **second** cooked catalog from that folder (engine-owned, not emitted by the game’s `asset_codegen`). Game codegen scans **only** the game `assets/` tree.
- Codegen is given the builtin GUID list and **fails** if a game `.meta` reuses one.
- Public constants: `include/engine/builtin_ids.h` (`engine::builtin::shader_unlit`, `mesh_quad`, `material_unlit`, `font_ui`). **Do not regenerate these GUIDs.**

Games still `Get<IMaterial>(engine::builtin::material_unlit)` (or a game `.mat` that references a game texture + builtin shader GUID).

---



## 11. Audio



### 11.1 Why not ping-pong audio

Ping-pong `AudioEventsManager`: `PlaySoundEvent{name}` → `MIX_PlayAudio` (no gain/pitch/stop); music is a single `MIX_Track`. That is not reusable for remakes with UI clicks, overlapping SFX, or music transitions.

Target model follows Lumenwake `IAudioSystem` / `SoundData` / SFX pool / dual music sources / looping handles, mapped onto SDL3_mixer:


| Lumenwake              | This engine                           |
| ---------------------- | ------------------------------------- |
| `AudioClip`            | decoded `MIX_Audio` inside the audio importer |
| `SoundData`            | `Sound` from `importer: audio` `.meta`        |
| `AudioSource`          | `MIX_Track`                           |
| `AudioMixer` groups    | linear bus gains × `MIX_SetTrackGain` |
| DOTween fade           | lerp in `IAudioSystem::Update(dt)`    |
| Zenject `IAudioSystem` | Boost.DI `IAudioSystem`               |




### 11.2 `Sound`

Produced by `AssetsDb::Get<Sound>(AssetId)`, not hand-filled in game code.

```cpp
struct Sound {
    std::shared_ptr<Audio> clip;     // decoded WAV
    float volume = 1.f;              // from .meta
    glm::vec2 pitchRange {1.f, 1.f}; // from .meta
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

    virtual bool Init() = 0;
    virtual void Dispose() = 0;
    virtual void Update(float dt) = 0;   // fades, recycle SFX tracks

    virtual void PlaySfx(const Sound& sound, float volumeScale = 1.f) = 0;

    virtual void PlayMusic(const Sound& sound, bool loop = true, float fadeSeconds = 0.f) = 0;
    virtual void StopMusic(float fadeSeconds = 0.f) = 0;
    virtual bool IsMusicPlaying() const = 0;

    virtual LoopingSfxHandle CreateLoopingSfx() = 0;
    virtual void PlayLoopingSfx(LoopingSfxHandle, const Sound&, float fadeIn = 0.f) = 0;
    virtual void StopLoopingSfx(LoopingSfxHandle, float fadeOut = 0.f) = 0;
    virtual void ReleaseLoopingSfx(LoopingSfxHandle, float fadeOut = 0.f) = 0;

    virtual void SetMasterVolume(float) = 0;  // 0..1
    virtual void SetMusicVolume(float) = 0;
    virtual void SetSfxVolume(float) = 0;
};
```

`LoopingSfxHandle` is an opaque id (`0` = invalid), same idea as Lumenwake’s struct.

### 11.4 Internals

- **SFX pool:** ~12 `MIX_Track`s. `PlaySfx` acquires a free track (`!MIX_TrackPlaying`); if none, **skip** (no steal, no queue). Pitch via `MIX_SetTrackFrequencyRatio`. Gain: `master * sfxBus * sound.volume * volumeScale`.
- **Music A/B:** two tracks. `PlayMusic` with `fadeSeconds > 0` and something already playing crossfades (incoming gain 0→target, outgoing →0 then stop). Immediate play if fade is 0 or idle.
- **Looping registry:** handle → dedicated track; fade in/out in `Update`. Enough for later remakes (engines, ambience); tic-tac-toe may unused it.
- **Buses:** no Unity mixer. `finalGain = master * bus * voiceVolume`. Mute ≈ very small gain (SDL has no dB mixer).
- **Tick:** `Loop` calls `IAudioSystem::Update(frameDt)` every **frame** (wall-clock fades), not once per fixed step. Ping-pong never ticked audio.
- **Format:** WAV only (mixer flags: OGG off).



### 11.5 Event queues (SFX)

```cpp
struct PlaySfxEvent { engine::AssetId id; float volumeScale = 1.f; };
struct PlayMusicEvent { engine::AssetId id; bool loop = true; float fadeSeconds = 0.f; };
```

An audio system in `Phase::Audio` (`EventReader<PlaySfxEvent>`): `Get<Sound>(id)` (fatal if the cue is required) then `IAudioSystem`. No filename events. Ping-pong `AudioEventsManager` is removed.

### 11.6 Game usage

```cpp
audio->PlayMusic(*db.Get<Sound>(assets::music::theme));
audio->PlaySfx(*db.Get<Sound>(assets::sfx::step));
```

`Get` returns `std::shared_ptr<T>` that is never null; the `*` is a reference to a live object, not a null check.

---



## 12. Testing (GoogleTest)

Ping-pong has no tests. This repo does. Tests live **in the engine**, not in each remake.

### 12.1 Targets and CMake

- Submodule: `external/googletest` (`https://github.com/google/googletest.git`).
- Binary: `engine_tests`, linked with `engine` + `GTest::gtest_main`.
- `enable_testing()`, `include(GoogleTest)`, `gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)` (same as Q+).
- MSVC: `gtest_force_shared_crt ON`; `INSTALL_GTEST OFF`; `BUILD_GMOCK OFF` until a test needs `NiceMock`.

`ENGINE_BUILD_TESTS` defaults to **ON** when this repo is the CMake root, **OFF** when a game does `add_subdirectory(engine)` — so remakes do not compile gtest unless they pass `-DENGINE_BUILD_TESTS=ON`.

```bash
# from engine/
cmake -S . -B ./build
cmake --build build --target engine_tests
ctest --test-dir build --output-on-failure
```

A tiny dummy `IGame` (or tests that never call `Engine::Run`) is enough to link the static lib. Do not boot a real window in default CI.

### 12.2 What to cover (required)

Prefer **pure logic** and fakes over GPU/mixer. Extract policy (gain, pool, AABB) so it is testable without `MIX_Track`.

| Area | Tests |
|---|---|
| ECS | generational Entity; try_get after destroy is empty; view<A,B>; deferred destroy during iteration |
| Event queues | send; reader sees current+previous; `FlushEvents` drops older than two frames; first `ctx<Events<T>>` registers T |
| CommandBuffer | push `CmdDrawMesh` (material, not raw shader) / `CmdDrawUI`; execute order; clear between frames; **no** custom-callback |
| Sort | layer, order_in_layer, material, entity; stable; UI commands after world |
| Materials | parse `.mat` TOML; missing shader GUID fails codegen; instance color multiplies |
| UiCanvas | FillWindow rect on resize; hit-test in rect; order; MouseConsumed reset each frame |
| UI XML/CSS | parse subset; unknown element fatal; `{Binding}` missing name fatal; CSS unknown prop warn |
| MVVM | Property/Command registration; OneWay bind updates label text; Button click calls ICommand; onClick API absent |
| Loop / Time | fixed-step accumulator; cap at `MAX_FIXED_STEPS`; **paused** → 0 Fixed steps, accumulator frozen |
| Physics | integrate velocity with `fixedDeltaTime`; AABB overlap; `CollisionEvent` on enter, not every stay frame |
| Camera | `ScreenToWorld` / orthographic bounds for a known window size |
| Input | binding scancode → action string; unbound key ignored (synthetic `SDL_Event` if possible, else a small mapper unit) |
| Audio policy | `finalGain = master * bus * voice`; clamp 0..1; SFX pool acquire/release; skip when pool exhausted; music A/B index swap on crossfade; invalid `LoopingSfxHandle` is a no-op |
| Assets / meta | parse texture + audio **TOML**; reject bad GUID; cooked catalog guid→path+importer; `TryGet` NotFound vs TypeMismatch; `Get` calls fatal hook; identifier from path; **codegen fails** if `.meta` missing; collision fails |

Test files: `tests/<area>_test.cpp` (`ecs_test.cpp`, `events_test.cpp`, `audio_test.cpp`, `assets_test.cpp`, …). One fixture per area is enough.

### 12.3 What not to cover in `engine_tests`

- Pixel-perfect OpenGL / NanoVG screenshots.
- `Engine<GameT>::Init` + real SDL window (needs a display; optional local `ENGINE_MANUAL_GL_TEST`).
- Decoding a WAV through SDL_mixer in CI (no audio device). Use a fake `Audio` / fake track for pool tests.
- Gameplay (bot AI, score) — that belongs in `tic-tac-toe` later if at all.

### 12.4 Fakes

Do not `#include` glad in tests. For audio, a `FakeTrack` / `FakeMixer` (or a pool templated on a `Track` concept) keeps §11 behavior under test without `MIX_Init`. Command execution can record calls instead of drawing.

A feature in §6 / §8 / §10 / §11 / ECS / events is **not done** until `engine_tests` has a case for the happy path and the main failure (empty pool, unknown GUID, missing `.meta` at codegen, unknown XML tag, missing binding name).

---

## 13. Coding conventions

- Namespaces: `engine`, `engine::ecs`, `engine::render`, `engine::ui`.
- Files: `snake_case` (`input_system.cpp`). **Public** headers under `include/engine/…`. **Private** headers live next to `.cpp` under `src/…` (not mirrored into `include/`). Tests: `tests/<area>_test.cpp`.
- Game aliases (optional, ping-pong style): `e`, `er`, `ecs`, `eui`.
- `.clang-format`: LLVM-based, 4 spaces, column 120 (copy from ping-pong).
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



## 15. Delta from ping-pong (checklist for extraction)

1. Move `external/` **into** this repo; engine CMake adds those subdirectories.
2. Fix nanovg include to `external/nanovg/src` (ping-pong root points at a non-existent `src/`).
3. `IGame::WindowTitle` / `WindowSize`; `Engine` uses them.
4. Replace `AudioSystem` (mixer-only) + `AudioEventsManager` with §11.
5. `Loop` ticks `IAudioSystem::Update`.
6. Bind `IAudioSystem` in DI instead of exposing `MIX_Mixer*` to games.
7. Add `external/googletest`, `external/tomlplusplus`, `tests/`, `engine_tests` (§12). No EnTT package.
8. GUID `AssetsDb`: TOML `.meta`, `asset_guid` + `asset_codegen`, `Get` / `TryGet` (§10).
9. Replace `EventBus` with Bevy-style `Events<T>` (§9). Homemade ECS with EnTT-like API (§7).
10. Remove `Node` / `NodeEcs` / `NodeUI`. UI = `UiCanvas` + XML document + ViewModel (§4.3, §8). Delete ping-pong `onClick`.
11. Fixed timestep Loop + `IGame::OnFixedUpdate` + `Schedule` / `Phase` (§4.4–§4.5). Variable `dt` into physics is not copied.
12. Delete `CmdCustomDraw`. `CmdDrawMesh` carries `IMaterial`, not shader+texture (§6).
13. Public `include/engine/` vs private `src/` headers; glm PUBLIC, SDL/glad/spdlog/NanoVG not (§3.4).
14. Sort `Renderable` by layer / order_in_layer / material / entity (§6.3).
15. Builtin assets + well-known GUIDs (§10.8).
16. `Events<T>` in `World::ctx`; `FlushEvents` at start of frame (§9).
17. Pause skips Fixed and freezes accumulator; resize updates `FillWindow` canvases (§4.6–§4.7).

---



## 16. Architectural rules (do not regress)

1. Game logic does not include glad / SDL render / mixer / NanoVG / spdlog / tinyxml2. Host includes `engine.h`. Game targets do not add `engine/src` to their include path (§3.4).
2. Draw only through `CommandBuffer`. The public variant has **no** custom GL callback. World draws use `IMaterial`, not loose shader/texture on the command.
3. Load only through `AssetsDb` by `AssetId`. Gameplay uses `Get` (fatal). Optional content uses `TryGet` and handles `AssetError`.
4. Play audio only through `IAudioSystem` with a `Sound` that came from the audio importer (or a test double).
5. Simulation / gameplay cross-talk: **event queues** (`send` / `read` / `FlushEvents`), not observer `Subscribe` on `this`. **UI → game:** `ICommand` on a `ViewModel` only — no `onClick` in game code.
6. Shared engine behavior ships with a GoogleTest, not only a remake that “seems to work”.
7. Do not change an asset GUID after it is referenced. Move files with their `.meta`. Builtin GUIDs in `builtin_ids.h` are frozen.
8. `asset_codegen` never writes `.meta`. Missing sidecar is a **failed build**, not a random GUID in CI.
9. ECS is homemade, EnTT-shaped. **Do not add EnTT as a submodule.** Do not keep a Node graph beside World. No `Transform` parent in v1.
10. Simulation uses `fixedDeltaTime` on `Schedule::Fixed`. One-shot clicks run on `Schedule::Frame`, `Phase::Game` (§4.4–§4.5).
11. UI markup is XML + CSS assets. Games do not build visual trees in C++ (tests excepted).
12. All engine APIs: **main thread only**.
13. `MouseConsumed` is cleared at the start of each Loop iteration, not at the end.

---

## 17. Open items (not blocking this SDD)

- Persist bus volumes (settings file) — game concern.
- Physics filename typo `physcis_system` — rename on extract.
- GitHub remote for this repo; games currently use relative submodule `../engine`.
- Optional later: gmock for `IRenderBackend`; game-repo tests for tic-tac-toe AI.
- `GetSprite(id, name)` for `layout: multiple` atlases (schema reserved in §10.6).
- Packed asset bundles (still GUID-addressed; catalog would point inside a pak).
- Separate `Sound` / cue asset that references a clip GUID (one WAV, several banks) — still one file = one cue until that exists.
- `Transform` parent / world-matrix chain.
- CSS combinators, `%` units, `@media`, animations; WPF `ControlTemplate`, `VisualStateManager`, `IValueConverter`, `Mode=TwoWay`.
- `Renderable` `sort_mode = Y` (auto ground-sort) — not v1; use `order_in_layer`.
- Widget-as-ECS-entity (Bevy UI) — not v1; would replace the XML instance tree inside `UiCanvas`.

