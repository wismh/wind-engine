# Wind — agent playbook

A 2D C++ game engine for production titles. CMake target / C++ namespace: `engine`. Tech vault: `docs/tech/README.md` (start at `docs/tech/Home.md`). Architecture: `docs/tech/architecture/Principles.md`, `Scope.md`, `Boundaries.md`. Task codes are `wind-N`. `README.md` has build instructions. `CLAUDE.md` is the same playbook; keep the two in sync if you change either.

Where vault as-built notes disagree with code, **code wins** — update the note in the same change. Where a change fights Principles / Boundaries, the change is wrong.

## Every session

1. Read Principles and Boundaries. Open the tech notes for the area you are changing.
2. Implement the user's task. Do not rebuild the whole engine "while you're here".
3. Cover shared behavior with `engine_tests` / `ctest`. GPU / real-window / mixer-device tests stay out of `engine_tests` (Boundaries).
4. Do not copy an ECS-pools / `EventBus` / scene-graph `Node*` / `CmdCustomDraw` / `onClick` / filename-`get` design. `ui::Node` (UI builder) is allowed.
5. Do not add EnTT. Do not commit unless the user asks — and when you do, follow the `wind-git` skill (`.claude/skills/wind-git/SKILL.md`).

## Layout

- Public API: `include/engine/` only (`#include <engine/…>`).
- Implementation + private headers: `src/`.
- Tests: `tests/<area>_test.cpp`, target `engine_tests`.
- Builtin assets: `builtin_assets/` (well-known GUIDs, never regenerate).
- Third party: `external/` git submodules. No FetchContent. No EnTT.

## C++ — applies to `**/*.{h,hpp,cpp,c}`

C++23, 4 spaces, column 120, LLVM-based `.clang-format`. Namespaces: `engine`, `engine::ecs`, `engine::render`, `engine::ui`. Files `snake_case`. Types `PascalCase`. Members `trailing_`. `enum class`. No `using namespace`.

Headers games may include live under `include/engine/`. Private headers sit next to their `.cpp` under `src/` and are **not** on the public include path.

```cpp
// BAD — private type in a public header
#include <glad/glad.h>
#include <spdlog/spdlog.h>

// GOOD
#include <engine/ecs/world.h>
#include <engine/render/commands.h>
```

Do not call `gl*`, `MIX_*`, or `nvg*` from `include/` or tests. Engine APIs are main-thread only.

## CMake — applies to `CMakeLists.txt`

Static `engine`. `PUBLIC` include = `include/`. `PRIVATE` = `src/`. glm is `PUBLIC`; spdlog, tinyxml2, tomlplusplus are `PRIVATE`.

`ENGINE_BUILD_TESTS` is ON when this repo is the CMake root. `ENGINE_WITH_WINDOW` / `ENGINE_WITH_AUDIO` default OFF at engine root (`vs` preset); local presets `vs-window`, `vs-audio` turn them on. Games consuming this repo as a submodule (`add_subdirectory(external/engine)` + `engine_add_game(...)`) get `ENGINE_WITH_WINDOW` ON by default and `ENGINE_BUILD_TESTS` OFF.

Do not `FetchContent`. Do not add EnTT. Do not enable tinyxml2's `xmltest`. New sources are picked up automatically by `GLOB_RECURSE` under `src/` and `tests/*_test.cpp`.

## Spec discipline

Do not introduce: EnTT, scene-graph `Node` / `NodeEcs` / `NodeUI`, `EventBus` Subscribe/Emit, `CmdCustomDraw`, UI `onClick` lambdas, `get("file.png")`, a `Transform` parent, or a service locator (`Engine::get_audio()`). `ui::Node` is the UI document builder, not a scene graph.

Public headers may include glm. They must not include SDL, glad, NanoVG, spdlog, tinyxml2, or mixer. UI → game is `ICommand` on a `ViewModel`. Draw through `CommandBuffer` with `IMaterial` on meshes. Assets by `AssetId` only.

## Compatibility

Never preserve a legacy shape "so existing call sites keep working." Breaking changes are always fine and preferred over a compat shim; update every call site in this repo and in consuming games instead.

- Don't keep a singleton/primary field alongside a new map/set for the same data just so old call sites compile unchanged (e.g. `MouseConsumed::value` + `consumed_windows`, `ctx<WindowSize>()` + `ctx<WindowSizes>()`). Pick one representation and migrate every caller to it.
- Don't add an enum value or config knob to a private/internal type without wiring it all the way through the public API meant to select it (e.g. `OverlayMode::AlwaysDisabled` with no `EngineRuntime` setter). Either finish the wiring in the same change or don't add the value yet.
- When a refactor's stated goal is "make X agnostic/generic," the old special case must actually disappear — not survive as the unconditional `Auto` default with no way to opt out.

## Git

This repo is **Wind**. Task code `wind-N`, branch `feat/wind-N-short-kebab`, from `main`. Every commit on that branch starts with the same `wind-N`.

```
wind-2 feat: add generational ecs entities
wind-2 test: cover try_get after destroy
```

Author `wismh <68060501+wismh@users.noreply.github.com>`. Commit dates: weekday clock time after the previous commit, `+0300`, **year 2023** — not today's real date; set both `GIT_AUTHOR_DATE` and `GIT_COMMITTER_DATE`. Do not change `git config`. No remote until the user asks; do not force-push `main` as a surprise. Do not commit `build/`, `cmake-build-*/`, `.vs/`, `.idea/`, generated catalogs, or secrets.

**Do not append a `Co-Authored-By` / `Claude-Session` trailer to commits in this repo.** Write the commit message as just `wind-N kind: subject`, nothing appended. Full detail lives in the `wind-git` skill: `.claude/skills/wind-git/SKILL.md`. Use it whenever the user asks to commit or push.
