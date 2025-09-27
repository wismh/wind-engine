# Game Lifecycle

Every Wind game centers around a concrete game class that derives from `engine::GameBase` (or implements `engine::IGame`) and is launched with the `ENGINE_GAME` macro, which runs the `engine::Engine<T>` application template.

---

## 1. Application Entrypoint

In `main.cpp`:

```cpp
#include <engine/game_entry.h>

#include "game.h"

ENGINE_GAME(game::MyGame)
```

In a normal build `ENGINE_GAME` expands to this `main`:

```cpp
int main() {
    engine::Engine<game::MyGame> app;
    if (!app.init()) {
        return 1;
    }
    return app.run();
}
```

- `app.init()`: Initializes platform subsystems (SDL, windowing, audio device, OpenGL context, `AssetsDb`) through `engine::EngineHost`. Returns `false` on failure.
- `app.run()`: Enters the main frame loop. It runs until the application receives a close event, returning the process exit code.

When the game is built for the editor (`build-editor`, configured against the SDK), the same line instead writes the three exports the editor loads the game module through (`wind_create_game`, `wind_destroy_game`, `wind_game_build_id`), so `main.cpp` does not change between Play and Export. `ENGINE_GAME` also checks at compile time that the class derives from `engine::IGame` and is constructible from `const engine::EngineServices&`.

---

## 2. `GameBase` and `IGame`

The `engine::IGame` interface governs window initialization and callbacks:

```cpp
namespace engine {

class IGame {
public:
    virtual ~IGame() = default;

    // Declares the initial main window
    virtual WindowDesc primary_window() const { return {}; }

    // Optional application window icon
    virtual std::optional<AssetId> window_icon() const { return std::nullopt; }

    // Active ECS World
    virtual ecs::World& world() = 0;

    // Called once after engine initialization and splash screen completion
    virtual void on_start() = 0;

    // Called right before shutdown and resource destruction
    virtual void on_quit() = 0;
};

}
```

`engine::GameBase` provides a standard default implementation of `IGame`, owning a primary `ecs::World` allocated through `Worlds`:

```cpp
class MyGame final : public engine::GameBase {
public:
    explicit MyGame(const engine::EngineServices& services)
        : engine::GameBase(services.worlds)
        , services_(services) {}

    engine::WindowDesc primary_window() const override {
        engine::WindowDesc desc;
        desc.title = "Star Quest";
        desc.size = {1280, 720};
        desc.style.resizable = true;   // flags are in WindowDesc::style: borderless, always_on_top, ...
        return desc;
    }

    void on_start() override;
    void on_quit() override;

private:
    const engine::EngineServices& services_;
};
```

---

## 3. `EngineServices`

The `EngineServices` reference is provided by `Engine<T>` when constructing your game instance:

```cpp
struct EngineServices {
    AssetsDb& assets;                  // Asset registry and loader
    InputSystem& input;                // Action mapping and input management
    IAudioSystem& audio;              // Sound effects and music
    IHaptics& haptics;                // Device vibration
    IHttpClient& http;                // HTTP requests
    IProcessLauncher& processes;      // Child processes
    IWindowControl& windows;          // Window management and frame pacing
    render::IGraphicFactory& graphics; // Low-level graphics allocation
    render::IRenderBackend& backend;   // Render backend
    render::ICanvas& canvas;           // 2D canvas primitives
    render::CommandBuffer& commands;   // Render command buffer
    Worlds& worlds;                    // Process worlds container
};
```

> [!TIP]
> The engine owns all underlying service objects. Store only the references you need as private members in your `Game` class.

---

## 4. Lifecycle Hooks

### `on_start()`
Invoked once, on the main thread, when the engine is fully initialized (window open, catalogs loaded) and before the first frame. The engine shows no splash screen by itself (`engine::ui::show_splash` is there if you want one).
- Register all ECS systems on `world()`.
- Bind gameplay actions via `services_.input.bind(...)`.
- Spawn initial entities (camera, UI canvases, game scenes).
- Construct UI view models and panels.

### `on_quit()`
Invoked once on the main thread when the main loop ends, before the engine's services are destroyed.
- Persist state or trigger final saves.
- Release game-specific non-RAII resources.

The engine never quits on its own when the player closes the window: it sends `engine::ui::WindowCloseRequestedEvent` and your game decides. To end the game, call `worlds().application_state().quit()` (a `GameBase` has `worlds()`); the loop stops after the current frame and `on_quit()` runs.

```cpp
for (const engine::ui::WindowCloseRequestedEvent& event : engine::ecs::EventReader<engine::ui::WindowCloseRequestedEvent>{
             world, world.ctx<engine::ecs::EventCursor<engine::ui::WindowCloseRequestedEvent>>()}) {
    if (event.window == engine::kPrimaryWindow) {
        worlds().application_state().quit();   // or show a "Quit?" dialog first
    }
}
```

---

## 5. Architectural Rule: Deferred Optional Initialization

In Wind, `std::optional` is used solely to defer construction until `on_start()`:

```cpp
class MyGame final : public engine::GameBase {
    // ...
private:
    std::optional<HudPanel> hud_;
};

void MyGame::on_start() {
    // Construct HUD in on_start
    hud_.emplace(world(), services_);
}
```

> [!IMPORTANT]
> Once `on_start()` has completed, `hud_` is guaranteed to exist. **Never** write null checks like `if (hud_) hud_->update();`. Call `hud_->update()` directly. If you need to check whether a UI element or window is visible, inspect real domain state (e.g. `hud_->is_open()`).

---

## Next Steps

- Learn how frame and fixed timesteps operate in [World & Time](World-and-Time.md).
- Understand persistent user data in [Saves & Filesystem](Saves-and-Filesystem.md).
