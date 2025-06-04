# Resources

GUID catalog, TOML `.meta`, `AssetsDb`, and the cook tools. Games never load by filename.

The runtime walkthrough is [Assets](../features/Assets.md). The cook tools are [Asset Codegen](../build/Asset%20Codegen.md).

## Identity

`AssetId` is exactly 32 lowercase hex characters (`include/engine/resources/asset_id.h`). The constructor throws `std::invalid_argument` when the string is not valid. `parse` returns `nullopt` instead.

Builtin ids in `include/engine/builtin_ids.h` are frozen. `builtin::reserved()` is the six ids `asset_codegen` rejects in a game tree. Do not regenerate them. See `builtin_assets/README.md`.

| Constant | GUID suffix | File |
| --- | --- | --- |
| `shader_unlit` | `…abc01` | `shaders/unlit.shader` |
| `mesh_quad` | `…abc02` | `meshes/quad.mesh` |
| `material_unlit` | `…abc03` | `materials/unlit.mat` |
| `font_ui` | `…abc04` | `fonts/ui.ttf` |
| `splash_wind` | `…abc05` | `textures/splash.png` |
| `font_math` | `…abc06` | `fonts/math.otf` |

The full literals are in `builtin_ids.h`. The table's suffix is the last five characters.

## `.meta`

`parse_asset_meta` reads TOML into `AssetMeta`.

| `ImporterKind` | Sidecar `importer` | Runtime type (typical) |
| --- | --- | --- |
| `Texture` | `texture` | GPU texture via `IGraphicFactory` |
| `UiImage` | `ui_image` | `render::TextureDesc` bytes for NanoVG |
| `Audio` | `audio` | `Sound` |
| `Mesh` | `mesh` | `IMesh` |
| `Shader` | `shader` | `IShader` |
| `Font` | `font` | `Font` (file bytes) |
| `Material` | `material` | `IMaterial` |
| `Ui` | `ui` | `UiDocument` |
| `Css` | `css` | `Stylesheet` |
| `Animation` | `animation` | `SpriteAnimationClip` |
| `Strings` | `strings` | `loc::StringTable` |

Texture defaults: sRGB, linear filter, clamp wrap, `layout = single`, `pixels_per_unit = 100`. Audio defaults: SFX bank, volume 1, pitch 1–1, loop false.

`layout = "multiple"` stores `[[sprites]]` rects. `SpriteSheet::get(name)` turns a rect into UV `tiling` and `offset` (Y flipped against the texture height). `AssetsDb::get_sprite(id, name)` uses that. An empty name on a single-layout texture is the whole image.

`strings_source` is the `.meta` flag `source = true`. If the tree contains any `.strings` file, codegen requires exactly one such table. A `{tr}` key must exist in that table.

`asset_guid` writes a missing sidecar and never overwrites one. `asset_codegen` fails the build when a sidecar is missing. It never writes `.meta`.

## `AssetsDb`

`get<T>` calls `try_get` and, on failure, `IFatalError::report` then does not return (`fail_get` is `[[noreturn]]`).

`try_get` returns `std::expected<std::shared_ptr<T>, AssetError>`:

| `AssetError` | When |
| --- | --- |
| `NotFound` | GUID absent from the cooked catalog |
| `Corrupt` | file or decode failed |
| `TypeMismatch` | `T` is not the importer's type |
| `NotReady` | a GPU type was requested and `set_graphic_factory` is null |

Results are cached by `(AssetId, type_index)`.

`load_catalog` parses a cooked `catalog.toml` and stamps each entry's `files_root`. `EngineHost::open_primary` loads `assets/engine/catalog.toml` (fatal on failure). `EngineHost::load_catalog` then loads `assets/catalog.toml` (a missing file is `MetaError::Io` and is ignored).

`unload_catalog(files_root)` removes the entries loaded with that files root (`CookedCatalog::remove_root`) and evicts every cached asset with one of their GUIDs, of any type. Entries from other roots, such as the engine builtins, and their cache stay. An unknown root is a no-op. The editor unloads the game catalog when a game stops.

`set_root` is the directory files are opened from. It is not the process working directory. See [Runtime Assets](../build/Runtime%20Assets.md).

## Codegen outputs

`codegen_write` writes two files into the output directory and nothing else:

- `asset_ids.h` — `constexpr AssetId` per relative path, plus a binder struct for each `importer = "ui"` document.
- `catalog.toml` — cooked entries (guid, relative path, importer, the texture or audio settings the runtime needs).

`identifier_from_path` turns `textures/x.png` into `assets::textures::x`. The same stem in two folders is two namespaces.

UI binders: [Asset Codegen](../build/Asset%20Codegen.md).

## Public headers

- `include/engine/builtin_ids.h`
- `include/engine/resources/asset_id.h`
- `include/engine/resources/asset_guid.h`
- `include/engine/resources/assets_db.h`
- `include/engine/resources/meta.h`
- `include/engine/resources/fatal_error.h`
- `include/engine/resources/font.h`
- `include/engine/resources/sprite_sheet.h`

`write_missing_metas` is the only function in `asset_guid.h`. Cook scan and write live on `meta.h` (`codegen_scan`, `codegen_write`) so the host tools and the tests can call them without a private header.

## Tools and tests

- `tools/asset_codegen/main.cpp` — `asset_codegen <assets_dir> <output_dir> [--engine]`
- `tools/asset_guid/main.cpp` — `asset_guid <assets_dir>`
- `tests/assets_test.cpp`, `tests/builtin_test.cpp`, `tests/sprite_test.cpp`

## See also

- [Principles](../architecture/Principles.md)
- [Localization](Localization.md)
