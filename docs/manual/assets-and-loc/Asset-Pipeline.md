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

Example `player.png.meta`:
```toml
guid = "7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d"
importer = "texture"

[settings]
filter = "nearest" # "linear" or "nearest"
wrap = "clamp"     # "clamp" or "repeat"
```

### Generating GUIDs
To assign a fresh unique GUID, use the `asset_guid` tool:
```bash
asset_guid assets/
```
The tool identifies newly added asset files that lack a `.meta` companion and generates one automatically.

---

## 2. Supported Importers

| Importer | Supported Extensions | Runtime Type |
| --- | --- | --- |
| `texture` | `.png`, `.jpg`, `.jpeg` | `render::ITexture` |
| `ui` | `.xml` | `ui::UiDocument` |
| `css` | `.css` | `ui::Stylesheet` |
| `audio` | `.wav`, `.ogg`, `.mp3` | `engine::Sound` |
| `strings` | `.strings` | `loc::StringTable` |
| `shader` | `.shader` | `render::IShader` |
| `material` | `.mat` | `render::IMaterial` |

---

## 3. Codegen Outputs (`asset_ids.h`)

During CMake configuration, `asset_codegen` processes your asset directory and produces:

1. `<build>/generated/<game>/asset_ids.h`:
   Contains compile-time `engine::AssetId` constants organized by directory hierarchy:
   ```cpp
   namespace assets::textures {
       inline constexpr engine::AssetId player{/* baked GUID */};
   }
   namespace assets::audio {
       inline constexpr engine::AssetId sfx_jump{/* baked GUID */};
   }
   ```
2. `<build>/generated/<game>/catalog.toml`:
   A cooked asset catalog containing GUID mappings and metadata loaded at startup.

---

## Next Steps

- Access cooked resources at runtime with [Loading Assets](Loading-Assets.md).
- Translate in-game text using [Localization](Localization.md).
