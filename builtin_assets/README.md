# Builtin assets

Default unlit shader, unit quad, unlit material, UI font, and math font.

**GUIDs are frozen** in `include/engine/builtin_ids.h` (`engine::builtin::*`). Do not regenerate them. Sidecar `.meta` files must keep those exact `guid` values.

| Constant | File | Importer |
| --- | --- | --- |
| `shader_unlit` | `shaders/unlit.shader` | `shader` |
| `mesh_quad` | `meshes/quad.mesh` | `mesh` |
| `material_unlit` | `materials/unlit.mat` | `material` |
| `font_ui` | `fonts/ui.ttf` | `font` |
| `font_math` | `fonts/math.otf` | `font` |

CMake copies this tree to `<exe dir>/assets/engine/`. Game `asset_codegen` is given this reserved GUID list and fails if a game `.meta` reuses one.

`fonts/ui.ttf` is a tiny SIL Open Font License face (Tiny5) so the GUID always has a committed raw file.

`fonts/math.otf` is STIX Two Math (v2.0.2, SIL OFL, `fonts/LICENSE.stix2math.txt`, source https://github.com/stipub/stixfonts `archive/STIXv2.0.2/OTF/STIX2Math.otf`): an OpenType MATH font whose `MATH` table drives formula layout.
