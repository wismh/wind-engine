# Builtin assets

Default unlit shader, unit quad, unlit material, UI font, math font, splash image, tree chevron, and the dock chrome theme.

**GUIDs are frozen** in `include/engine/builtin_ids.h` (`engine::builtin::*`). Do not regenerate them. Sidecar `.meta` files must keep those exact `guid` values.

| Constant | File | Importer |
| --- | --- | --- |
| `shader_unlit` | `shaders/unlit.shader` | `shader` |
| `mesh_quad` | `meshes/quad.mesh` | `mesh` |
| `material_unlit` | `materials/unlit.mat` | `material` |
| `font_ui` | `fonts/ui.ttf` | `font` |
| `splash_wind` | `textures/splash.png` | `ui_image` |
| `font_math` | `fonts/math.otf` | `font` |
| `tree_chevron` | `textures/tree_chevron.png` | `ui_image` |
| `dock_css` | `css/dock.css` | `css` |

CMake copies this tree to `<exe dir>/assets/engine/`. Game `asset_codegen` is given this reserved GUID list and fails if a game `.meta` reuses one.

`fonts/ui.ttf` is Inter Regular 4.1, the static TrueType instance (not the variable font), SIL OFL (`fonts/OFL.txt`, https://github.com/rsms/inter/releases/tag/v4.1, `extras/ttf/Inter-Regular.ttf`). It is the default UI face, including Cyrillic. The GUID stays `font_ui`.

`fonts/math.otf` is STIX Two Math (v2.0.2, SIL OFL, `fonts/LICENSE.stix2math.txt`, source https://github.com/stipub/stixfonts `archive/STIXv2.0.2/OTF/STIX2Math.otf`): an OpenType MATH font whose `MATH` table drives formula layout.

`textures/tree_chevron.png` is a 32×32 grey (`#8a8f98`) chevron pointing right, made for this repo. A tree row rotates it 90° when its node is expanded ([UI](../docs/tech/modules/UI.md#trees)).

`css/dock.css` is the default look of dock space chrome: tab strips, tabs and their close buttons, splitters, float frames and titles, the drop preview. The engine places every box through `--x`, `--y`, `--w`, `--h` ([Docking](../docs/tech/features/Docking.md#host)) and sizes each tab from `.dock-tab`'s font and padding; `--reserve` keeps a closable tab's title clear of its close button ([Tab width](../docs/tech/features/Docking.md#tab-width)).
