# Rules & Conventions

To maintain a clean codebase and ensure seamless upgrades when moving to a new engine SDK, adhere to these coding standards.

---

## 1. Engine Boundaries

- **Includes:** Game code must include **only** `<engine/...>`.
  - **Never** include engine private headers (`src/...` of the engine; the SDK's `include/` has only the public ones).
  - **Never** directly include third-party dependencies used by the engine (`SDL3`, `glad`, `NanoVG`, `spdlog`, `tinyxml2`, `SDL_mixer`).
- **Main Thread Only:** Engine APIs (`World`, `AssetsDb`, `CommandBuffer`, `UiCanvas`) are not thread-safe and must be invoked exclusively from the main thread.
- **SDK Discipline:** Your game builds against an installed Wind SDK (`find_package(Wind)`); its version is your engine version. Never edit the SDK. An engine change goes to the engine repo and reaches the game as a new SDK.

---

## 2. C++23 Style Guide

- **Formatting:** 4 spaces for indentation, 120-column limit.
- **Naming Conventions:**
  - Types / Classes / Structs: `PascalCase` (`PlayerController`, `InventoryView`)
  - Functions / Methods: `snake_case` (`update_movement()`, `on_start()`)
  - Member Variables: `trailing_` (`speed_`, `world_`)
  - Constants: `kPascalCase` (`kMaxPlayers`, `kDefaultSpeed`)
  - Enums: Scoped `enum class` (`enum class Alignment { Left, Right };`)
  - UI View-Model Bound Fields: `camelCase` (`healthText`, `togglePause`)
- **Namespaces:** Place all game code in the `game` namespace (or nested sub-namespaces). Never write `using namespace engine;` or `using namespace std;`.
- **One Type Per File:** Avoid placing multiple large classes in a single header/source file. If a source file exceeds 500 lines, consider refactoring.

---

## 3. ECS Architecture Discipline

- **No Scene-Graph Hierarchy:** Do not add a `parent` pointer or `children` array to `Transform`. World transformations are absolute coordinates. Hierarchical attachments should be resolved explicitly through relationship components or systems.
- **No Direct EventBus Subscriptions:** Do not build a monolithic publish/subscribe bus with arbitrary callback lambdas. Use double-buffered `Events<T>` queues read by systems during frame updates.
- **No Third-Party ECS Integration:** Do not pull EnTT into game code; use Wind's built-in generational `ecs::World`.
- **No Service Locator:** Take what you need from `EngineServices` in your game's constructor and keep the references; there is no `Engine::get_audio()`.
- **UI Talks to Game Through Commands:** A button is an `ICommand` on a view model, bound by `command="{binding name}"`. There is no `onClick` lambda in markup or in the builder.
- **Assets by `AssetId` Only:** Load with `AssetsDb::get<T>(assets::folder::name)`, never by file name.
- **Gameplay Input by `ActionId`:** Bind keys with `InputSystem::bind`; do not switch on `KeyCode` in gameplay.

---

## Next Steps

- Avoid critical runtime mistakes in [Common Pitfalls](Common-Pitfalls.md).
- Write decoupled unit tests with [Testing Game Code](Testing-Game-Code.md).
