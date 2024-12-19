---
tags: [module]
---

# Resources

GUID catalog, TOML `.meta`, `AssetsDb`, codegen. Games never load by filename.

## Capabilities

- `AssetId`: 32 lowercase hex chars. Frozen once referenced. Builtin ids in [[include.engine.builtin_ids.h]] must not be regenerated ([[builtin_assets/README.md]]).
- Sidecar `.meta` (TOML) holds importer settings. `asset_guid` writes missing sidecars; `asset_codegen` is read-only and **fails** if meta is missing ([[build/Asset Codegen]]).
- Runtime loads the cooked catalog. `get<T>` is fatal via `IFatalError`; `try_get<T>` returns `AssetError` ([[include.engine.resources.assets_db.h]]).
- Sprite sheets: `.meta` `layout = "single"` or `"multiple"` with `[[sprites]]`. `get_sprite(id, name)` / `try_get_sprite` resolve a named rect on the atlas (`SpriteSheet::get`); a single-layout texture also works as a one-sprite sheet ([[include.engine.resources.sprite_sheet.h]]).
- `ASSETS_PATH` is the executable directory, not cwd.

## Public headers

- [[include.engine.resources.asset_id.h]]
- [[include.engine.resources.asset_guid.h]]
- [[include.engine.resources.assets_db.h]]
- [[include.engine.resources.meta.h]]
- [[include.engine.resources.fatal_error.h]]
- [[include.engine.resources.font.h]]
- [[include.engine.resources.sprite_sheet.h]]

## Tools

- [[tools/asset_guid/README.md]]
- [[tools/asset_codegen/README.md]]

## See also

- [[architecture/Principles]]
- [[build/Runtime Assets]]
