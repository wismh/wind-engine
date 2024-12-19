---
tags: [moc]
aliases: [MOC, Wind tech]
---

# Home


## Layers

```mermaid
flowchart LR
  A["[[architecture/Overview]]"] --> M["Modules"]
  M --> F["Features"]
  F --> B["[[build/Pipeline]]"]
  M --> Files["[[files/_Index]]"]
```

1. **[[architecture/Overview|Architecture]]** — what exists, who owns it, how it connects. Rules: [[architecture/Principles]], [[architecture/Scope]], [[architecture/Boundaries]].
2. **Modules** — what each area can do and how it is built:
   - [[modules/Core]]
   - [[modules/ECS]]
   - [[modules/Resources]]
   - [[modules/Render]]
   - [[modules/UI]]
   - [[modules/Audio]]
   - [[modules/Haptics]]
3. **Features** — implementation walkthroughs (as-built):
   - [[features/UI Markup]]
   - [[features/UI Input]]
   - [[features/Input Mapper]]
   - [[features/Windowing]]
4. **[[build/Pipeline|Build pipeline]]** — configure → codegen → compile → copy assets → run.
5. **[[files/_Index|File index]]** — every first-party `.h` / `.cpp` / tool / test.

## Architecture shortcuts

- [[architecture/Runtime Loop]]
- [[architecture/Module Map]]
- [[architecture/Boundaries]]
- [[architecture/Principles]]
- [[architecture/Scope]]

## Build shortcuts

- [[build/CMake]]
- [[build/Asset Codegen]]
- [[build/Icon Codegen]]
- [[build/Runtime Assets]]
- [[build/Game Consumer]]

## Mental model

A game is an `IGame` (usually `GameBase`). `Engine<GameT>` (windowed) constructs services with Boost.DI, loads catalogs into [[include.engine.resources.assets_db.h|AssetsDb]], registers [[include.engine.ecs.systems.h|engine systems]], then [[include.engine.core.engine_runtime.h|EngineRuntime]] runs the loop. Games never call OpenGL; they spawn ECS components and push work through events / UI commands. The backend executes a [[include.engine.render.command_buffer.h|CommandBuffer]].
