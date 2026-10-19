# Asset codegen

Two host programs. Both link `engine` and call code that also ships in the library (`src/resources/codegen.cpp`, `src/resources/asset_guid.cpp`).

## `asset_codegen`

```
asset_codegen [<assets_dir> <output_dir> [--engine]] [--components <output_file> [<source_dir>...]]
```

Usage errors exit 2. A cook error prints the message and exits 1.

| Mode | Reserved GUIDs |
| --- | --- |
| default | `builtin::reserved()` (the eleven engine ids) |
| `--engine` | none |

Asset mode writes only:

- `<output_dir>/asset_ids.h`
- `<output_dir>/catalog.toml`

`--components` scans C++ headers and sources under the given directories for `struct WIND_COMPONENT` definitions and writes `<output_file>` (`component_io.h`).

It does not write `.meta`.

`CodegenErrorKind`:

| Kind | When |
| --- | --- |
| `MissingMeta` | a known asset file has no sidecar |
| `InvalidGuid` | the sidecar GUID is not 32 lowercase hex |
| `Collision` | two files share a GUID, a game GUID is in the reserved set, two `{binding}` paths hash equal, or two `WIND_COMPONENT` structs map to the same snake_case key |
| `InvalidMeta` | TOML or importer fields failed |
| `Io` | the tree or an output file could not be read or written |
| `UiMarkup` | a UI document failed the bind scan |
| `Strings` | a `.strings` file failed to parse, the tree has any string table but not exactly one `source = true`, a document uses `{tr}` and there is no source table, or a `{tr}` key is absent from that source table |
| `World` | a `.world` file failed TOML parsing or structural validation (`version`, duplicate entity IDs, missing parent, hierarchy cycle, missing `active_camera`) |
| `ComponentCodegen` | a `struct WIND_COMPONENT` declaration is malformed (e.g. unclosed brace) |

CMake runs this for `builtin_assets/` (`--engine`) and for each `engine_add_game` target (both `assets/` when present and `--components` into `generated/<target>/component_io.h`). See [Pipeline](Pipeline.md).

### Identifiers

`identifier_from_path` (`include/engine/resources/meta.h`):

| Relative path | C++ name |
| --- | --- |
| `textures/x.png` | `assets::textures::x` |
| `materials/board.mat` | `assets::materials::board` |

The same stem in two directories is two namespaces.

### UI binders

For `importer = "ui"`, the scan (`src/ui/bind_scan.h`) records every `{binding}` path. The header gets a struct named from the document with one `static constexpr engine::ui::BindingId` per path and `bind(vm)` that registers `vm.property` or `vm.command` from the member of the same name.

The game writes the `ViewModel` and the `Bindable` fields. Codegen does not.

### Component codegen

`include/engine/resources/component_codegen.h`, `src/resources/component_codegen.cpp`.

Game structs marked with `WIND_COMPONENT` (`include/engine/ecs/world_io.h`) are scanned for non-static data member identifiers. `emit_component_io_header` writes `component_io.h` with `assets::register_game_components(engine::ecs::WorldSerializer&)`, registering each `PascalCase` struct under its `snake_case` TOML table key and emitting `out.field("name", c.name)` / `(void)in.field("name", c.name)` calls per field.

## `asset_guid`

```
asset_guid <assets_dir>
```

`write_missing_metas` creates a sidecar for each file that lacks one, except skipped files (dotfiles, `readme.md`, `license*`, `catalog.toml`): a new GUID and the default importer (audio defaults for `.wav`). A file with an unknown extension gets `importer = "data"`. Existing sidecars are left alone. The tool prints how many files it wrote. It is not a build step. Commit the new `.meta`, then cook.

## See also

- [Assets](../features/Assets.md)
- [Resources](../modules/Resources.md)
- [tools/asset_codegen/README.md](../../../tools/asset_codegen/README.md)
- [tools/asset_guid/README.md](../../../tools/asset_guid/README.md)
