# Quickstart

This guide walks you through setting up a minimal game using the Wind engine, opening a window, and running your main loop.

---

## 1. Prerequisites

- **CMake 3.25+**
- A **C++23** conforming compiler (MSVC 19.36+, Clang 16+, GCC 13+)
- **Git**

---

## 2. Setting Up the Repository

Initialize your game repository and add the Wind engine as a git submodule in `external/engine`:

```bash
mkdir my_game
cd my_game
git init
git submodule add https://github.com/wismh/wind-engine.git external/engine
git submodule update --init --recursive
```

---

## 3. Minimal `CMakeLists.txt`

Create a root `CMakeLists.txt` file:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Wind engine as a submodule
add_subdirectory(external/engine)

# Declare your game executable
engine_add_game(my_game
    src/main.cpp
    src/game.h
    src/game.cpp
)
```

`engine_add_game` configures C++23, enables windowing and audio by default, triggers asset compilation (`asset_codegen`), and copies cooked assets next to the output executable.

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

`ENGINE_GAME` writes `main` for you. It runs `engine::Engine<game::MyGame>`: `init`, then `run`.

---

## 5. Build and Run

Configure and build your game:

```bash
cmake -B build -S .
cmake --build build --config RelWithDebInfo
```

Run the resulting executable located in `build/` (or `build/RelWithDebInfo/my_game.exe` on Windows). You should see a window titled "My First Wind Game" open up!

---

## Next Steps

- Learn about [Project Structure](Project-Structure.md) for organizing your assets, domain code, and UI.
- Learn about [CMake Integration](CMake-Integration.md) to manage presets and platform builds.
- Understand [Game Lifecycle](../architecture/Game-Lifecycle.md) to start adding gameplay logic.
