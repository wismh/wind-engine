---
tags: [build]
---

# Runtime assets

Layout **next to the executable** after POST_BUILD (`engine_prepare_runtime`):

```
your-game.exe
assets/
  catalog.toml          ← cooked game catalog
  textures/…
  materials/…
  ui/…
  engine/
    catalog.toml        ← cooked builtin catalog
    shaders/unlit.shader
    meshes/quad.mesh
    materials/unlit.mat
    fonts/ui.ttf
```

`EngineRuntime::assets_root()` is `<base>/assets`. Init loads:

1. `assets/engine/catalog.toml` with file root `assets/engine`
2. `assets/catalog.toml` with file root `assets`

Builtin GUIDs in code: [[include.engine.builtin_ids.h]]. Do not regenerate.

Game `Get` uses generated [[build/Asset Codegen|asset_ids.h]] constants.

If the game has no `assets/` folder, `engine_add_game` skips game codegen; only engine builtins are staged.

## See also

- [[build/Pipeline]]
- [[features/Init and Loop]]
- [[features/Assets]]
