# Builtin assets (SDD §10.8)

Default unlit shader, unit quad, unlit material, and UI font.

**GUIDs are frozen** in `include/engine/builtin_ids.h` (`engine::builtin::*`). Do not regenerate them. Sidecar `.meta` files must keep those exact `guid` values.

| Constant | File | Importer |
| --- | --- | --- |
| `shader_unlit` | `shaders/unlit.shader` | `shader` |
| `mesh_quad` | `meshes/quad.mesh` | `mesh` |
| `material_unlit` | `materials/unlit.mat` | `material` |
| `font_ui` | `fonts/ui.ttf` | `font` |

CMake copies this tree to `<exe dir>/assets/engine/`. Game `asset_codegen` is given this reserved GUID list and fails if a game `.meta` reuses one.

`fonts/ui.ttf` is a tiny SIL Open Font License face (Tiny5) so the GUID always has a committed raw file.
