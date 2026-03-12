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

A second `load_catalog` adds entries. It does not replace the engine catalog.

Where `<assets>` is: [Runtime Assets](../build/Runtime%20Assets.md).

## Load

`set_graphic_factory` must be set before a GPU type can load. A null factory yields `AssetError::NotReady`.

`get<T>` reports through `IFatalError` and does not return. `try_get<T>` returns the error.

`get_sprite(id, name)` resolves a named rect. `try_get_sprite` returns `AssetError` instead of calling the fatal hook. An empty name on a single-layout texture is the whole image (`offset` 0, `tiling` 1,1). An empty name when `layout` is `multiple` is `AssetError::NotFound`.

UI documents and stylesheets are CPU assets (`UiDocument`, `Stylesheet`). Fonts are the file bytes (`Font`). UI images used by NanoVG are `render::TextureDesc` (`importer = "ui_image"`), uploaded per window when a canvas references them. World textures go through the graphic factory.

The cache key is the GUID plus `std::type_index`. The same GUID requested as two types is two loads, and the wrong type is `TypeMismatch`.

## Builtin set

Six frozen GUIDs. The splash image is `importer = "ui_image"`. The list is [Resources](../modules/Resources.md) and `builtin_assets/README.md`.

## See also

- [Asset Codegen](../build/Asset%20Codegen.md)
- [Materials and Sort](Materials%20and%20Sort.md)
- [Localization](../modules/Localization.md)
