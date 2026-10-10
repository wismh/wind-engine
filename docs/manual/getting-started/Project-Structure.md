# Project Structure

A clean, predictable project layout ensures that your code, assets, and tests remain decoupled and easy to maintain.

---

## Recommended Directory Tree

```
my_game/
├── CMakeLists.txt                 # Root CMake configuration
├── wind_project.toml              # Makes the directory a project the editor opens (name, engine, target)
├── CMakePresets.json              # Editor preset (DebugGame;Release)
├── CMakeUserPresets.json          # Not committed: CMAKE_PREFIX_PATH to the Wind SDK
├── icon.png                       # Optional root icon (processed by icon_codegen)
│
├── assets/                        # Raw game assets
│   ├── textures/
│   │   ├── hero.png
│   │   └── hero.png.meta          # Asset GUID metadata sidecar
│   ├── audio/
│   │   ├── sfx_jump.wav
│   │   └── sfx_jump.wav.meta
│   ├── loc/
│   │   └── en.strings             # Localization string tables
│   ├── ui/
│   │   └── hud.xml                # Declarative UI markup (each asset has a .meta beside it)
│   └── css/
│       └── hud.css                # CSS styling (its own folder: hud.xml and hud.css would both be assets::ui::hud)
│
├── src/
│   ├── main.cpp                   # Application entrypoint
│   ├── game.h                     # Main Game class inheriting GameBase
│   ├── game.cpp
│   │
│   ├── domain/                    # Pure gameplay / physics / math logic (NO engine headers)
│   │   ├── inventory.h
│   │   └── inventory.cpp
│   │
│   ├── systems/                   # ECS systems
│   │   ├── player_system.h
│   │   └── player_system.cpp
│   │
│   ├── ui/                        # ViewModels and UI controllers
│   │   ├── hud_view_model.h
│   │   └── hud_panel.cpp
│   │
│   └── components/                # ECS components (pure structs)
│       └── gameplay_components.h
│
└── tests/
    ├── CMakeLists.txt             # Unit tests target
    └── domain_test.cpp            # Tests for src/domain/ (fast, engine-free)
```

---

## Architectural Principles for Structure

### 1. Public Boundary
Games include headers **only** from `<engine/...>`:
```cpp
// GOOD
#include <engine/engine.h>
#include <engine/ecs/world.h>
#include <engine/render/sprite.h>

// FORBIDDEN - Do not include the engine's private headers (src/...) or its third-party libraries
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <nanovg.h>
#include <spdlog/spdlog.h>
```

### 2. Isolated Domain Logic (`src/domain/`)
Keep game math, state machines, and simulation rules independent of engine rendering and windowing:
- Files in `src/domain/` should not include engine headers (except lightweight header-only utilities like `KeyCode` if necessary).
- This enables compiling fast unit tests in `tests/` without spinning up engine subsystems or linking SDL/OpenGL.

### 3. Asset Files & `.meta` Sidecars
Every asset placed in `assets/` must have a corresponding `.meta` file containing a unique 128-bit GUID:
```toml
# assets/textures/hero.png.meta
guid = "a1b2c3d4e5f60718293a4b5c6d7e8f90"
importer = "texture"
```
During the build, `asset_codegen` scans `assets/` and generates compile-time identifiers in `<build>/generated/my_game/asset_ids.h`.

---

## Next Steps

- Check [CMake Integration](CMake-Integration.md) for configuring targets and build configurations.
- Read [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md) to understand metadata and cooking.
