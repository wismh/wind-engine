# Game Lifecycle

Every Wind game centers around a concrete game class that derives from `engine::GameBase` (or implements `engine::IGame`) and is launched via the `engine::Engine<T>` application template.

---

## 1. Application Entrypoint

In `main.cpp`:

```cpp
#include <engine/engine.h>
#include <engine/core/engine.h>

#include "game.h"

int main() {
    engine::Engine<game::MyGame> app;
    if (!app.init()) {
        return 1;
    }
    return app.run();
}
```

- `app.init()`: Initializes platform subsystems (SDL, windowing, audio device, OpenGL context, `AssetsDb`). Returns `false` on failure.
- `app.run()`: Enters the main frame loop. It runs until the application receives a close event, returning the process exit code.

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
        desc.resizable = true;
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
    IHaptics& haptics;                // Force feedback & vibration
    IWindowControl& windows;          // Secondary window management
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
Invoked when the engine is fully initialized and the initial splash screen has completed.
- Register all ECS systems on `world()`.
- Bind gameplay actions via `services_.input.bind(...)`.
- Spawn initial entities (camera, UI canvases, game scenes).
- Construct UI view models and panels.

### `on_quit()`
Invoked on the main thread when a close event is triggered or shutdown is requested.
- Persist state or trigger final saves.
- Release game-specific non-RAII resources.

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
