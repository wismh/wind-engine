# Asset codegen

Two host programs. Both link `engine` and call code that also ships in the library (`src/resources/codegen.cpp`, `src/resources/asset_guid.cpp`).

## `asset_codegen`

```
asset_codegen <assets_dir> <output_dir> [--engine]
```

Usage errors exit 2. A cook error prints the message and exits 1.

| Mode | Reserved GUIDs |
| --- | --- |
| default | `builtin::reserved()` (the six engine ids) |
| `--engine` | none |

Writes only:

- `<output_dir>/asset_ids.h`
- `<output_dir>/catalog.toml`

It does not write `.meta`.

`CodegenErrorKind`:

| Kind | When |
| --- | --- |
| `MissingMeta` | a known asset file has no sidecar |
| `InvalidGuid` | the sidecar GUID is not 32 lowercase hex |
| `Collision` | two files share a GUID, a game GUID is in the reserved set, or two `{binding}` paths hash equal |
| `InvalidMeta` | TOML or importer fields failed |
| `Io` | the tree or an output file could not be read or written |
| `UiMarkup` | a UI document failed the bind scan |
| `Strings` | a `.strings` file failed to parse, the tree has any string table but not exactly one `source = true`, a document uses `{tr}` and there is no source table, or a `{tr}` key is absent from that source table |

CMake runs this for `builtin_assets/` (`--engine`) and for each `engine_add_game` target that has an `assets/` directory. See [Pipeline](Pipeline.md).

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

## `asset_guid`

```
asset_guid <assets_dir>
```

`write_missing_metas` creates a sidecar for each known file that lacks one: a new GUID and the default importer (audio defaults for `.wav`). Existing sidecars are left alone. The tool prints how many files it wrote. It is not a build step. Commit the new `.meta`, then cook.

## See also

- [Assets](../features/Assets.md)
- [Resources](../modules/Resources.md)
- [tools/asset_codegen/README.md](../../../tools/asset_codegen/README.md)
- [tools/asset_guid/README.md](../../../tools/asset_guid/README.md)
