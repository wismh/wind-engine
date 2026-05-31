# Assets

Cooked files addressed by `AssetId`. Filenames stay in the asset tree and in the cooked catalog. Game code uses the generated constants.

## On disk

A source file and a sibling `.meta`. The sidecar is TOML.

```
guid = "0123456789abcdef0123456789abcdef"
importer = "texture"
```

`asset_guid <assets_dir>` writes a sidecar only when one is missing. It prints how many files it created. It never overwrites. It is not part of the game build.

`asset_codegen` refuses to run when a known file has no sidecar (`CodegenErrorKind::MissingMeta`).

Importer strings and defaults: [Resources](../modules/Resources.md).

## Cooked catalog

`catalog.toml` is a list of entries the runtime can open: guid, relative path, importer, and the texture or audio settings copied from `.meta`. `CookedCatalog::find` is a linear search.

Two catalogs are loaded, in order:

| File | File root | Missing |
| --- | --- | --- |
| `<assets>/engine/catalog.toml` | `<assets>/engine` | fatal |
| `<assets>/catalog.toml` | `<assets>` | ignored (`MetaError::Io` only) |

A second `load_catalog` adds entries. It does not replace the engine catalog. `unload_catalog(files_root)` removes one root's entries and their cached assets again. See [Resources](../modules/Resources.md).

Where `<assets>` is: [Runtime Assets](../build/Runtime%20Assets.md).

## Load

`set_graphic_factory` must be set before a GPU type can load. A null factory yields `AssetError::NotReady`.

`get<T>` reports through `IFatalError` and does not return. `try_get<T>` returns the error.

`get_sprite(id, name)` resolves a named rect. `try_get_sprite` returns `AssetError` instead of calling the fatal hook. An empty name on a single-layout texture is the whole image (`offset` 0, `tiling` 1,1). An empty name when `layout` is `multiple` is `AssetError::NotFound`.

UI documents and stylesheets are CPU assets (`UiDocument`, `Stylesheet`). A stylesheet's parser warnings go to the engine log on load, as `stylesheet <relative path>: line N: <warning>` ([UI Markup](UI%20Markup.md)). Fonts are the file bytes (`Font`). Worlds are parsed TOML descriptions (`ecs::WorldAsset`, `importer = "world"`; see [ECS](../modules/ECS.md#world-serialization)). UI images are `render::TextureDesc` (`importer = "ui_image"`), packed into the UI renderer's image atlas when a canvas references them. World textures go through the graphic factory.

The cache key is the GUID plus `std::type_index`. The same GUID requested as two types is two loads, and the wrong type is `TypeMismatch`.

## Text assets

`importer = "data"` is for files the engine does not know: item configs, tables, anything text. `get<TextAsset>(id)` returns the file bytes in `TextAsset::text`, unchanged (no newline conversion, no parse). The game parses it with whatever it likes. A file that does not read is `AssetError::Corrupt`. The file name and extension do not matter, `asset_guid` gives every file with an unknown extension this importer, and a `.meta` can name it for any other file.

## Mesh files

`importer = "mesh"` reads a `.mesh` text file into a `render::MeshDesc` (`parse_mesh`, `src/resources/importers.cpp`). One row per line; `#` starts a comment and blank lines are skipped.

| Row | Meaning |
| --- | --- |
| `px py pz u v` | A vertex with normal `0 0 1`. The original format; a file of only these rows still loads |
| `px py pz nx ny nz u v` | A vertex with a normal, written as is (not normalized) |
| `f a b c` | One triangle of 0-based vertex indices, counter-clockwise when its front faces you |

Vertices are numbered in file order, and the two vertex forms may mix. A file with no `f` row is a non-indexed triangle list (every three vertices are a triangle). One `f` row makes the mesh indexed: only the `f` rows are triangles. A quad is two `f` rows.

The file does not load (`AssetError::Corrupt`) when a vertex row has other than 5 or 8 numbers or a non-number, an `f` row has other than three non-negative integers, a row starts with another word, there are no vertices, or an index is at or past the vertex count.

```
# pos.x pos.y pos.z   normal.x normal.y normal.z   uv.x uv.y
-0.5  0.5 0   0 0 1   0 1
-0.5 -0.5 0   0 0 1   0 0
 0.5  0.5 0   0 0 1   1 1
 0.5 -0.5 0   0 0 1   1 0
f 0 1 2
f 1 3 2
```

Meshes on the GPU: [Render](../modules/Render.md#meshes).

## Builtin set

Eleven frozen GUIDs. The splash image and the tree chevron are `importer = "ui_image"`. The list is [Resources](../modules/Resources.md) and `builtin_assets/README.md`.

## See also

- [Asset Codegen](../build/Asset%20Codegen.md)
- [Materials and Sort](Materials%20and%20Sort.md)
- [Localization](../modules/Localization.md)
