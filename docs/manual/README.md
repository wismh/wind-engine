# Wind Game Developer Manual

Welcome to the **Wind** Game Developer Manual. This documentation is written for game developers building 2D games with the Wind game engine.

Wind is a high-performance 2D C++23 game engine tailored for production titles. It features a generational ECS, a declarative XML/CSS UI system with MVVM bindings, an asset cooking pipeline with compile-time identifiers, multi-window presentation, 2D physics probes, and cross-platform support across Desktop, Web (WASM/WebGL2), and Android.

---

## Table of Contents

### 1. [Getting Started](getting-started/Quickstart.md)
* [Quickstart](getting-started/Quickstart.md) — Create and run your first game from scratch.
* [Project Structure](getting-started/Project-Structure.md) — Recommended file layout for a game repository.
* [CMake Integration](getting-started/CMake-Integration.md) — `engine_add_game`, options, and build presets.

### 2. [Core Architecture](architecture/Game-Lifecycle.md)
* [Game Lifecycle](architecture/Game-Lifecycle.md) — `ENGINE_GAME`, `Engine<Game>`, `EngineServices`, `on_start`, and `on_quit`.
* [World & Time](architecture/World-and-Time.md) — Delta time, fixed simulation stepping (`FixedStepClock`), and pause handling.
* [Saves & Filesystem](architecture/Saves-and-Filesystem.md) — Cross-platform persistent storage via `user_data_directory`.

### 3. [Entity Component System (ECS)](ecs/Entities-and-Components.md)
* [Entities & Components](ecs/Entities-and-Components.md) — Generational IDs, pools, `World`, and `View<Ts...>`.
* [Systems & Schedules](ecs/Systems-and-Schedules.md) — `Schedule::Frame`, `Schedule::Fixed`, and execution phases.
* [Events](ecs/Events.md) — Double-buffered event queues (`Events<T>`, `EventReader`, `EventWriter`).
* [Physics & Collisions](ecs/Physics-and-Collisions.md) — AABB colliders, triggers, and `run_physics` collision probes.

### 4. [Rendering & 2D Graphics](rendering/Sprites-and-Animation.md)
* [Sprites & Animation](rendering/Sprites-and-Animation.md) — Sprites, spritesheets, UV mapping, and clip animators.
* [Materials & Shaders](rendering/Materials-and-Shaders.md) — Material definitions (`.mat`), GLSL shaders in CDATA, and uniforms.
* [Command Buffer & Sorting](rendering/Command-Buffer-and-Sort.md) — Queuing draw commands, renderable layers, and sort predicates.
* [Camera](rendering/Camera.md) — Orthographic camera, zoom, and screen-to-world coordinate conversion.

### 5. [Wind UI System](ui/UI-Basics.md)
* [UI Basics](ui/UI-Basics.md) — Declarative XML markup, CSS styling, and `UiCanvas`.
* [MVVM & Data Binding](ui/MVVM-and-Bindings.md) — `ViewModel`, properties, `camelCase` binding names, and codegen.
* [HUD & Controls](ui/HUD-and-Controls.md) — Panels, HUD command pattern, `TextInput`, and virtualized `ItemsControl`.
* [Custom Painting](ui/Custom-Painting.md) — Custom 2D vector drawing using `IPaint` and `IDrawList`.
* [Math Formulae](ui/Math-Formulae.md) — Native LaTeX mathematical typography via `<Math>`.

### 6. [Input Handling](input/Action-Mapping.md)
* [Action Mapping](input/Action-Mapping.md) — Named `ActionId` bindings, multi-device mappings, and input cursors.
* [Raw Input & Mouse](input/Raw-Input-and-Mouse.md) — `MouseEvent`, held key repeat (`KeyEvent`), and the `MouseConsumed` latch.
* [Multi-Window Input](input/Multi-Window-Input.md) — Window routing, overlay focus, and click-through prevention.

### 7. [Audio & Haptics](audio-and-haptics/Sound-and-Music.md)
* [Sound & Music](audio-and-haptics/Sound-and-Music.md) — SFX pool, music fading, looping sound handles, and audio buses.
* [Haptics](audio-and-haptics/Haptics.md) — Device vibration, duration, and intensity control.

### 8. [Assets & Localization](assets-and-loc/Asset-Pipeline.md)
* [Asset Pipeline](assets-and-loc/Asset-Pipeline.md) — The `assets/` directory, `.meta` sidecars, GUIDs, and `asset_codegen`.
* [Loading Assets](assets-and-loc/Loading-Assets.md) — `AssetsDb`, type-safe `get<T>` and `try_get<T>`.
* [Localization](assets-and-loc/Localization.md) — `.strings` message catalogs, fallback locales, and `{tr}` UI patterns.

### 9. [Target Platforms](platforms/Desktop.md)
* [Desktop](platforms/Desktop.md) — Exporting a Release build from the editor or in batch mode (CI), exporting by hand, and distribution on Windows, Linux, and macOS.
* [Web (WebAssembly)](platforms/Web-Wasm.md) — Emscripten, WebGL2, host codegen prerequisite, and asset preloading.
* [Android](platforms/Android.md) — Gradle packaging, application identifiers, assets staging, and back button.

### 10. [Best Practices & Conventions](best-practices/Rules-and-Conventions.md)
* [Rules & Conventions](best-practices/Rules-and-Conventions.md) — C++23 standards, naming, file boundaries, and architecture rules.
* [Common Pitfalls](best-practices/Common-Pitfalls.md) — Crucial architectural and runtime gotchas to avoid.
* [Testing Game Code](best-practices/Testing-Game-Code.md) — Writing decoupled game logic tests without compiling engine internals.
