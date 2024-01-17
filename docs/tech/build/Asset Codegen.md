---
tags: [build]
---

# Asset codegen

Two programs, both link `engine` (they reuse [[src.resources.codegen.cpp]] / [[src.resources.asset_guid.cpp]]).

## `asset_codegen`

[[tools.asset_codegen.main.cpp]]:

```
asset_codegen <assets_dir> <output_dir> [--engine]
```

Read-only. Writes `asset_ids.h` and `catalog.toml` into `output_dir`. Game mode: GUID must not be in `builtin::reserved()`. `--engine`: empty reserved set.

CMake: engine builtins and `engine_add_game` both depend on this executable + a glob of asset files (`CONFIGURE_DEPENDS` so new files retrigger).

Failure modes: missing `.meta`, bad GUID, collision, IO. Non-zero exit fails the build.

For `importer = "ui"` XML, also scans `{binding}` paths ([[src.ui.bind_scan.h]]) and emits a binder struct (e.g. `Hud::bind(vm)`) plus one `constexpr BindingId` per path into the same header. Two paths hashing to the same `BindingId` fails codegen (`CodegenErrorKind::Collision`). Does not generate `ViewModel` classes or `Bindable<T>` fields — those stay hand-written; the game calls the generated `bind(vm)` from its own `ViewModel` constructor.

## `asset_guid`

[[tools.asset_guid.main.cpp]]: create sidecars for files that lack them. Dev tool, not part of the default game build.

## Identifiers

`textures/x.png` → `assets::textures::x`. `materials/board.mat` → `assets::materials::board`. Same stem in different folders is OK (different namespaces).

## See also

- [[features/Assets]]
- [[modules/Resources]]
- [[build/Runtime Assets]]
- [[src.resources.codegen.cpp]]
