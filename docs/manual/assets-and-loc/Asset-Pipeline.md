# Asset Pipeline

Wind avoids runtime file scanning and dynamic path lookups. Instead, assets are assigned immutable GUIDs, declared alongside `.meta` sidecars, and cooked during the build by `asset_codegen`.

---

## 1. Asset Metadata Files (`.meta`)

Every source asset file in `assets/` requires a `.meta` sidecar in TOML format:

```
assets/
└── textures/
    ├── player.png
    └── player.png.meta
```

Example `player.png.meta`. The import settings are plain top-level keys beside `guid` and `importer`, not a `[settings]` table:

```toml
guid = "7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d"   # exactly 32 lowercase hex characters
importer = "texture"

filter = "nearest"   # "linear" (default) or "nearest"
wrap = "clamp"       # "clamp" (default), "repeat" or "mirror"
```

| Importer | Keys besides `guid` and `importer` (defaults in brackets) |
| --- | --- |
| `texture` | `color_space` (`srgb`, `linear`), `filter`, `wrap`, `layout` (`single`, `multiple`), `pixels_per_unit` (100), `[[sprites]]` for `multiple` ([Sprites & Animation](../rendering/Sprites-and-Animation.md#2-sprite-sheets-and-atlases)) |
| `audio` | `bank` (`sfx`, `music`), `volume` (1), `pitch_range` (`[1.0, 1.0]`), `loop` (false) |
| `strings` | `source = true` on exactly one table of the tree ([Localization](Localization.md)) |

An unknown key is ignored. A wrong value (`filter = "bilinear"`) is `MetaError::InvalidField` and fails the cook. Once a `guid` is referenced, never change it: move the asset together with its `.meta`.

### Generating GUIDs
`asset_codegen` never writes a `.meta`: a source file without a sidecar fails the build. To create sidecars for new files, use the `asset_guid` tool of the engine repo:
```bash
asset_guid assets/
```
It writes a `.meta` (a fresh GUID and the default importer for the extension) for every known file that lacks one, and never overwrites an existing one. Commit the new sidecars, then build.

> [!NOTE]
> The installed SDK does not contain `asset_guid` (it ships `asset_codegen`, `icon_codegen`, `wind-cli`, and the editor). Build it in the engine repo (`cmake --build build-editor --config Release --target asset_guid`), call `engine::write_missing_metas(root)` from `<engine/resources/asset_guid.h>` in a tool of your own, or write the sidecar by hand with any unique GUID.

---

## 2. Supported Importers

`asset_guid` picks the importer from the file extension; `asset_codegen` reads the `importer` named in the sidecar.

| Importer | Extension | Runtime type for `AssetsDb::get<T>` |
| --- | --- | --- |
| `texture` | `.png` | `render::ITexture` (GPU texture), or `SpriteSheet` for the named sub-sprites |
| `ui_image` | `.png` (set by hand) | `render::TextureDesc`, the pixels a UI `<Image>` or `background-image` draws. A `texture` PNG can be used by the UI too |
| `audio` | `.wav` | `engine::Sound` |
| `ui` | `.xml` | `ui::UiDocument` |
| `css` | `.css` | `ui::Stylesheet` |
| `strings` | `.strings` | `loc::StringTable` |
| `shader` | `.shader` | `render::IShader` |
| `mesh` | `.mesh` | `render::IMesh` |
| `material` | `.mat` | `render::IMaterial` |
| `animation` | `.anim` | `render::SpriteAnimationClip` |
| `font` | `.ttf` | `Font` (the file bytes) |

The audio build decodes WAV only. Asking `get<T>` for the wrong `T` is `AssetError::TypeMismatch`.

---

## 3. Codegen Outputs (`asset_ids.h`)

During the build, `asset_codegen` processes your `assets/` directory and produces:

1. `<build>/generated/<game>/asset_ids.h`:
   Contains compile-time `engine::AssetId` constants. The namespace is `assets::` plus the folder path, the name is the file name without its last extension:
   ```cpp
   namespace assets::textures {
   inline constexpr engine::AssetId player{"7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d"};
   }
   namespace assets::audio {
   inline constexpr engine::AssetId sfx_jump{"..."};   // audio/sfx_jump.wav
   }
   ```
   Characters other than letters, digits, and `_` become `_`. Keep the stem of a file unique inside its folder and starting with a letter: `hud.xml` and `hud.css` in one folder would both be `assets::ui::hud`. Put stylesheets in their own folder (`assets/css/hud.css` is `assets::css::hud`).
   For every `ui` document the header also holds a binder struct, for example `assets::ui::Hud` ([MVVM & Data Binding](../ui/MVVM-and-Bindings.md#2-generated-binders)).
2. `<build>/generated/<game>/catalog.toml`:
   A cooked asset catalog containing GUID mappings and metadata loaded at startup.

The build fails (and prints why) on a missing `.meta`, a bad or duplicated GUID, a GUID reserved by the engine's builtin assets, a UI document that does not parse, and a `.strings` or `{tr}` problem ([Localization](Localization.md#4-ui-localization-via-tr)).

---

## Next Steps

- Access cooked resources at runtime with [Loading Assets](Loading-Assets.md).
- Translate in-game text using [Localization](Localization.md).
