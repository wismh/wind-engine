---
tags: [build]
---

# Consuming Wind from a game

Wind is a **git submodule** (e.g. `external/engine`, url `../engine`).

```cmake
set(ENGINE_WITH_AUDIO ON CACHE BOOL "" FORCE)  # optional
add_subdirectory(external/engine)
engine_add_game(my_game
    src/main.cpp
    src/game.cpp)
```

`engine_add_game`:

- `add_executable` + C++23 + MSVC warnings
- `PRIVATE engine`
- `include/` of the game if present
- asset codegen + `target_include_directories` generated dir
- `engine_prepare_runtime`

`main.cpp` typically:

```cpp
#include <engine/engine.h>
#include <game/game.h>
int main() {
    engine::Engine<game::Game> app;
    if (!app.init()) return 1;
    return app.run();
}
```

`Game` constructor is Boost.DI-injected (`AssetsDb&`, `InputSystem&`, `IAudioSystem&`, …). Prefer `GameBase`.

Pin the submodule to a Wind `main` commit; do not develop features inside the nested copy — edit the engine repo directly, then pin.

## See also

- [[build/Pipeline]]
- [[build/CMake]]
- [[features/Init and Loop]]
- [[include.engine.core.engine.h]]
- [[include.engine.igame.h]]
