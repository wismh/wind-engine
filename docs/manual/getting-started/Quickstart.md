# Quickstart

This guide walks you through setting up a minimal game using the Wind engine, opening a window, and running your main loop.

---

## 1. Prerequisites

- **CMake 3.20+**
- **Visual Studio 2026** (MSVC, C++23)
- **Git**
- A **Wind editor SDK**. Build it once from the engine repo:

```bash
git clone --recursive https://github.com/wismh/wind-engine.git
cd wind-engine
cmake --preset vs-editor
cmake --build build-editor --config Release
cmake --install build-editor --config Release --prefix out/sdk
```

`out/sdk/` now holds the editor (`bin/wind_editor.exe`), the engine, its headers, and the CMake package your game finds. `out/sdk/sdk.toml` names its engine version.

---

Shortcut: Wind Launcher's **New project…** writes sections 2 to 4 for you (an empty game with one window) from your SDK's template and opens it in the editor. Install the SDK where the launcher finds it, `%LOCALAPPDATA%\Programs\Wind\Sdks\<version>`, or Locate it on the launcher's SDKs page. The steps below are what that template contains.

---

## 2. Setting Up the Repository

Your game repository does not contain the engine. It finds the SDK when it configures:

```bash
mkdir my_game
cd my_game
git init
```

---

## 3. Minimal `CMakeLists.txt`

Create a root `CMakeLists.txt` file:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_game LANGUAGES CXX)

# The installed Wind SDK (CMAKE_PREFIX_PATH points at it)
find_package(Wind REQUIRED)

# Declare your game
engine_add_game(my_game
    src/main.cpp
    src/game.h
    src/game.cpp
)
```

`engine_add_game` configures C++23, triggers asset compilation (`asset_codegen`), and builds your game as a module (`my_game.dll`) that the editor loads on Play, with its cooked assets beside it.

Add `wind_project.toml` beside it. It makes the directory a project the editor opens, and names the engine version and the target to play:

```toml
name = "My First Wind Game"
engine = "0.1.0"   # the version in your SDK's sdk.toml
target = "my_game"
```

For building by hand in an IDE, also add `CMakePresets.json` (committed) and `CMakeUserPresets.json` (not committed: the SDK path is per machine). The editor does not need them:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "editor",
      "generator": "Visual Studio 18 2026",
      "binaryDir": "${sourceDir}/build-editor",
      "cacheVariables": { "CMAKE_CONFIGURATION_TYPES": "DebugGame;Release" }
    }
  ]
}
```

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "editor-local",
      "inherits": "editor",
      "cacheVariables": { "CMAKE_PREFIX_PATH": "C:/path/to/wind-engine/out/sdk" }
    }
  ]
}
```

---

## 4. Writing the Game Entrypoint

Create `src/game.h`:

```cpp
#pragma once

#include <engine/engine.h>
#include <engine/igame.h>

namespace game {

class MyGame final : public engine::GameBase {
public:
    explicit MyGame(const engine::EngineServices& services);

    [[nodiscard]] engine::WindowDesc primary_window() const override;

    void on_start() override;
    void on_quit() override;

private:
    const engine::EngineServices& services_;
};

} // namespace game
```

Create `src/game.cpp`:

```cpp
#include "game.h"

namespace game {

MyGame::MyGame(const engine::EngineServices& services)
    : engine::GameBase(services.worlds)
    , services_(services) {}

engine::WindowDesc MyGame::primary_window() const {
    engine::WindowDesc desc;
    desc.title = "My First Wind Game";
    desc.size = {1280, 720};
    desc.resizable = true;
    return desc;
}

void MyGame::on_start() {
    engine::log::info("Game started successfully!");

    // Register a frame-update system
    world().add_system(
        engine::ecs::Schedule::Frame,
        engine::ecs::Phase::Game,
        [](engine::ecs::World& w) {
            // Your frame update logic here
        }
    );
}

void MyGame::on_quit() {
    engine::log::info("Game is shutting down.");
}

} // namespace game
```

Create `src/main.cpp`:

```cpp
#include <engine/game_entry.h>

#include "game.h"

ENGINE_GAME(game::MyGame)
```

`ENGINE_GAME` writes the exports the editor loads your module through (and `main` in a standalone executable).

---

## 5. Build and Run

Open the project in the editor and play it (CMake must be on `PATH`). Wind Launcher does the same with a button: Add project, pick `wind_project.toml`, then Open. The launcher finds SDKs installed in `%LOCALAPPDATA%\Programs\Wind\Sdks`; for one elsewhere (your own `out/sdk`), use Locate on the SDKs page and pick its `sdk.toml`. Or directly:

```bash
C:/path/to/wind-engine/out/sdk/bin/wind_editor.exe --project . --play
```

Play builds your game (`DebugGame`: your code unoptimized with symbols) and loads it; the Build tab shows the output and the first error. The editor opens the game window titled "My First Wind Game" next to its own window. Stop unloads the game, so you can edit the code and press Play again.

---

## 6. Export a Release Build

Play runs your game inside the editor as a module. To get something you can give to players, press **Export** next to Play: the editor builds a standalone `Release` executable with the engine linked in and copies it, with its `assets/`, to `<project>/export/<target>/`. The first export takes minutes (it compiles the engine); later ones are fast. For a build server with no display, run `wind_editor --batch --project . --export out --log-file export.log` (exit code `0` on success). See [Desktop](../platforms/Desktop.md#1-exporting-from-the-editor).

---

## Next Steps

- Learn about [Project Structure](Project-Structure.md) for organizing your assets, domain code, and UI.
- Learn about [CMake Integration](CMake-Integration.md) to manage presets and platform builds.
- Understand [Game Lifecycle](../architecture/Game-Lifecycle.md) to start adding gameplay logic.
