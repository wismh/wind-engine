# Builtin assets

Default unlit shader, unit quad, unit cube, unlit material, Lambert lit shader and its white opaque material, texture-array variants of the lit and unlit shaders, skinned lit and unlit shaders, four fullscreen post shaders, the fluid surface composite, UI font, math font, splash image, tree chevron, and the dock chrome theme.

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
| `mesh_cube` | `meshes/cube.mesh` | `mesh` |
| `shader_lit` | `shaders/lit.shader` | `shader` |
| `material_lit` | `materials/lit.mat` | `material` |
| `shader_lit_array` | `shaders/lit_array.shader` | `shader` |
| `shader_unlit_array` | `shaders/unlit_array.shader` | `shader` |
| `shader_skin_lit` | `shaders/skin_lit.shader` | `shader` |
| `shader_skin_unlit` | `shaders/skin_unlit.shader` | `shader` |
| `shader_post_grade` | `shaders/post_grade.shader` | `shader` |
| `shader_post_tonemap` | `shaders/post_tonemap.shader` | `shader` |
| `shader_post_bloom` | `shaders/post_bloom.shader` | `shader` |
| `shader_post_depth` | `shaders/post_depth.shader` | `shader` |
| `shader_decal` | `shaders/decal.shader` | `shader` |
| `shader_fluid_surface` | `shaders/fluid_surface.shader` | `shader` |

CMake copies this tree to `<exe dir>/assets/engine/`. Game `asset_codegen` is given this reserved GUID list and fails if a game `.meta` reuses one.

`shaders/lit.shader` is Lambert diffuse per directional light plus ambient, from the uniforms the backend fills from `CmdSetView` ([Render](../docs/tech/modules/Render.md#lit-shader)). `shaders/unlit.shader` and `shaders/lit.shader` both multiply the albedo by the vertex color (location 3), discard below `uAlphaCutoff` (cutout), and apply the view's linear fog ([Cutout](../docs/tech/modules/Render.md#cutout), [Fog](../docs/tech/modules/Render.md#fog)). No uniform is declared in both stages of either shader (GLSL ES precision rule). Its light arrays are sized 4, the same as `engine::render::kMaxDirectionalLights`. `materials/lit.mat` uses it: white, no texture (the backend binds white), `blend = "opaque"`, `depth = true`, `cull = "back"`.

`shaders/lit_array.shader` and `shaders/unlit_array.shader` are the same two shaders with `uTexture` a `sampler2DArray` sampled at the vertex layer (location 4, `MeshVertex::layer`), for texture arrays ([Render](../docs/tech/modules/Render.md#texture-arrays)). `unlit_array` has no animated tiles. No uniform is declared in both stages.

`shaders/post_grade.shader`, `post_tonemap.shader`, `post_bloom.shader`, and `post_depth.shader` are fullscreen passes over the scene image ([Post process](../docs/tech/modules/Render.md#post-process)). The engine owns the vertex stage. `uScene` is the previous image, top-left origin. Grade multiplies rgb by `uParams.rgb` and copies alpha. Tonemap is Reinhard and is appended when `PostStack::hdr` is set. Bloom's `uParams.x` is the threshold and `uParams.y` the intensity; the backend drives `uParams.z`. Depth tints by `uDepth`.

`fonts/ui.ttf` is Inter Regular 4.1, the static TrueType instance (not the variable font), SIL OFL (`fonts/OFL.txt`, https://github.com/rsms/inter/releases/tag/v4.1, `extras/ttf/Inter-Regular.ttf`). It is the default UI face, including Cyrillic. The GUID stays `font_ui`.

`fonts/math.otf` is STIX Two Math (v2.0.2, SIL OFL, `fonts/LICENSE.stix2math.txt`, source https://github.com/stipub/stixfonts `archive/STIXv2.0.2/OTF/STIX2Math.otf`): an OpenType MATH font whose `MATH` table drives formula layout.

`textures/tree_chevron.png` is a 32×32 grey (`#8a8f98`) chevron pointing right, made for this repo. A tree row rotates it 90° when its node is expanded ([UI](../docs/tech/modules/UI.md#trees)).

`css/dock.css` is the default look of dock space chrome: tab strips, tabs and their close buttons, splitters, float frames and titles, the drop preview. The engine places every box through `--x`, `--y`, `--w`, `--h` ([Docking](../docs/tech/features/Docking.md#host)) and sizes each tab from `.dock-tab`'s font and padding; `--reserve` keeps a closable tab's title clear of its close button ([Tab width](../docs/tech/features/Docking.md#tab-width)).
