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

UI documents and stylesheets are CPU assets (`UiDocument`, `Stylesheet`). A stylesheet's parser warnings go to the engine log on load, as `stylesheet <relative path>: line N: <warning>` ([UI Markup](UI%20Markup.md)). Fonts are the file bytes (`Font`). Worlds are parsed TOML descriptions (`ecs::WorldAsset`, `importer = "world"`; see [ECS](../modules/ECS.md#world-serialization)). UI images are `render::TextureDesc` (`importer = "ui_image"`), packed into the UI renderer's image atlas when a canvas references them. World textures go through the graphic factory. An icon is `ui::SvgPicture` (`importer = "svg"`, [UI](../modules/UI.md#paint)).

The cache key is the GUID plus `std::type_index`. The same GUID requested as two types is two loads, and the wrong type is `TypeMismatch`.

## SVG icons

`importer = "svg"` reads an icon into `ui::SvgPicture` (`parse_svg`, `src/ui/svg_parser.cpp`). The file needs a `viewBox`. Shapes are `path`, `rect`, `circle`, `ellipse`, `line`, `polyline`, and `polygon`, and a `g` whose transform is baked into the points. `path` data is `M L H V C Q S T Z A`; an arc, a circle, and a rounded rect become cubics. `fill` and `stroke` are `none`, `currentColor`, or `#rgb` / `#rrggbb` / `#rrggbbaa`, inherited from the root and from groups. A dash, an even-odd fill, text, an image, a gradient, a clip, or a mask makes the asset `AssetError::Corrupt`. `asset_guid` assigns this importer to `.svg`.

## Tilemaps

`importer = "tilemap"` (`.tilemap`, assigned by `asset_guid`) is TOML with a tileset texture GUID, a cell `size`, a `tile_size`, and layers of tile indexes. `get<render::TilemapAsset>(id)` loads the file and its texture, so it needs a graphic factory (`AssetError::NotReady` without one); a file that does not parse, or whose tileset has no whole column, is `AssetError::Corrupt`, and a `texture` that is not in the catalog or is not a texture is that asset's error. Cook fails with `CodegenErrorKind::Tilemap` when the file does not parse. Format and drawing: [Render](../modules/Render.md#tilemaps).

## Text assets

`importer = "object"` (`.object`, assigned by `asset_guid`) is a TOML table with a string `type` plus the fields of one `WIND_SERIALIZABLE` struct. Cook fails when `type` is missing or the file is not a table. `AssetsDb::get<T>` returns that struct when a loader for `T` was registered and `type` equals its key. A different key or an unregistered `T` is `AssetError::TypeMismatch`. A file that does not parse, has no `type`, or has a field the loader rejects is `AssetError::Corrupt`. The loader is code from the game module: register it with `assets::register_game_objects` (a module does this from `wind_create_game` and from `wind_register_game_objects`; a static game calls it from the game constructor) and call `AssetsDb::clear_object_loaders` before the module is unloaded. See [Asset Codegen](../build/Asset%20Codegen.md#component-codegen) and [ECS](../modules/ECS.md#world-serialization).

`importer = "data"` is for files the engine does not know: item configs, tables, anything text. `get<TextAsset>(id)` returns the file bytes in `TextAsset::text`, unchanged (no newline conversion, no parse). The game parses it with whatever it likes. A file that does not read is `AssetError::Corrupt`. The file name and extension do not matter, `asset_guid` gives every file with an unknown extension this importer, and a `.meta` can name it for any other file.

## Mesh files

`importer = "mesh"` reads a `.mesh` text file into a `render::MeshDesc` (`parse_mesh`, `src/resources/importers.cpp`). One row per line; `#` starts a comment and blank lines are skipped.

| Row | Meaning |
| --- | --- |
| `px py pz u v` | A vertex with normal `0 0 1`. The original format; a file of only these rows still loads |
| `px py pz nx ny nz u v` | A vertex with a normal, written as is (not normalized) |
| `px py pz nx ny nz u v r g b a` | A vertex with a normal and a vertex color (`MeshVertex::color`). Rows without these four numbers are white |
| `f a b c` | One triangle of 0-based vertex indices, counter-clockwise when its front faces you |

Vertices are numbered in file order, and the three vertex forms may mix. A file with no `f` row is a non-indexed triangle list (every three vertices are a triangle). One `f` row makes the mesh indexed: only the `f` rows are triangles. A quad is two `f` rows.

The file does not load (`AssetError::Corrupt`) when a vertex row has other than 5, 8, or 12 numbers or a non-number, an `f` row has other than three non-negative integers, a row starts with another word, there are no vertices, or an index is at or past the vertex count.

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

## Texture arrays

`importer = "texture_array"` on a PNG's `.meta` makes the image a GPU texture array with one layer per `[[sprites]]` frame, in `.meta` order (the same rect syntax as a `layout = "multiple"` sheet; `layout`, `pixels_per_unit` and pivots are not used). `filter`, `wrap`, and `mipmaps` apply as to a texture. A block game writes `mipmaps = "linear"` and `filter = "nearest"`:

```toml
guid = "..."
importer = "texture_array"
filter = "nearest"
mipmaps = "linear"

[[sprites]]
name = "stone"
rect = [0, 0, 16, 16]

[[sprites]]
name = "dirt"
rect = [16, 0, 16, 16]
```

`get<render::TextureArray>(id)` (`include/engine/render/texture_array.h`) is the texture plus the frame names; `layer("dirt")` is the layer index for `MeshVertex::layer`, nullopt for an unknown name. `get<render::ITexture>(id)` is the same texture (`layers()` is the frame count), so a `.mat` `[textures].albedo` naming the asset binds the array. The PNG is only an array: `get_sprite`, `SpriteSheet`, and `TextureDesc` are `AssetError::TypeMismatch` for it, so 2D icons of the same art need their own texture.

Checks. Parsing the `.meta` fails (`MetaError::InvalidField`, so the cook fails with `InvalidMeta`) without frames, with more than 256 (`render::kMaxTextureArrayLayers`), with frames of different sizes, or with two frames of one name. `asset_codegen` also decodes the PNG and fails with `CodegenErrorKind::TextureArray` when a frame is not inside the image or the file is not a PNG. At load the same frame check makes the asset `AssetError::Corrupt` (the reason is logged). The importer is `texture_array_from_frames` (`src/resources/importers.h`). GPU side: [Render](../modules/Render.md#texture-arrays).

## Shaders

`importer = "shader"` (`.shader`, assigned by `asset_guid`) is an XML file containing Slang shader source (`<shader lang="slang"><![CDATA[...]]></shader>` or `<slang>`), an optional `<glsl><vertex>...</vertex><fragment>...</fragment></glsl>` backend override, or legacy `<xml><vertex>...</vertex><fragment>...</fragment></xml>` raw GLSL (`parse_shader` / `parse_shader_xml`, `src/resources/importers.cpp`). `asset_codegen` compiles and validates every `.shader` file and fails the build with `CodegenErrorKind::Shader` on syntax, type, or XML errors. See [Render](../modules/Render.md#shaders).

## Models

`importer = "model"` (`.gltf` and `.glb`, assigned by `asset_guid`) is a glTF 2 document. The runtime never parses glTF. `asset_codegen` cooks it, on the host, into one little-endian blob (magic `WMOD`, version 1) and stage writes that blob in place of the source. External buffers and images are embedded, so the game receives one file. The source in `assets/` stays glTF. One file is one GUID; the cooker writes no `.meta` and no extra meshes.

The cooker is `engine_gltf_cook` (`tools/gltf/`, cgltf in `external/cgltf`). It is linked into `asset_codegen` and `engine_tests`, not into `engine`, because `src/**/*.cpp` is the runtime on every target including Web and the dedicated server. `set_model_cooker` (`include/engine/resources/model_cook.h`) is the hook `codegen_scan` and `codegen_stage` call. A model that cannot be cooked fails the build with `CodegenErrorKind::Model`.

`get<render::ModelAsset>` needs no GPU. `spawn_model` uploads meshes and textures once a graphic factory exists. Channels (step, linear, cubicspline) are baked to keys at 30 Hz; the runtime only lerps adjacent keys. A missing channel holds the node's rest pose. A skin may have at most 128 joints (`kMaxSkinJoints`). More than four weights on a vertex are cut to the four largest and renormalized. Morphs, sparse accessors, Draco, texture transforms, and any `extensionsRequired` other than `KHR_materials_unlit` are errors. Materials are Lambert or unlit from `baseColorFactor`, `baseColorTexture`, `alphaMode`, and `doubleSided`. Metallic and roughness are ignored. The posed extent of every clip is stored as `MeshDesc::bounds_override` so frustum culling does not use the bind pose. Axes match the camera (Y up, looking down −Z); there is no axis conversion.

## Builtin set

Fifteen frozen GUIDs. The splash image and the tree chevron are `importer = "ui_image"`. The list is [Resources](../modules/Resources.md) and `builtin_assets/README.md`.

## Runtime textures

`AssetsDb::set_runtime_texture(id, desc)` keeps a `TextureDesc` made at runtime (the editor's thumbnails) under an id that is not in the catalog; `runtime_texture` finds it and `clear_runtime_textures` drops them all. The host's `ensure_ui_image` looks there before the catalog, so a UI `Image` or `background-image` can name such an id like any other. Main thread only. A window that already registered an id keeps those pixels until its UI cache is reset, so a changed picture needs a new id.

`<engine/resources/image_io.h>` is the CPU-only half of image work: `decode_png_rgba`, `encode_png_rgba`, and `fit_within(image, max_edge)` (linear downscale keeping the aspect; an image that fits is returned as it is). They touch no GPU and no `AssetsDb`, so a worker thread may call them.

## See also

- [Asset Codegen](../build/Asset%20Codegen.md)
- [Materials and Sort](Materials%20and%20Sort.md)
- [Localization](../modules/Localization.md)
