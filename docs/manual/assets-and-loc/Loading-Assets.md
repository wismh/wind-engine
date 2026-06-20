# Loading Assets

Wind loads and caches resources through `engine::AssetsDb`. Game code retrieves assets strictly using compile-time `AssetId` handles—never raw string file paths.

---

## 1. Type-Safe Asset Access

`AssetsDb` provides two retrieval methods:

```cpp
#include <engine/audio/sound.h>
#include <engine/log.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

// 1. get<T>: reports through the fatal-error hook and does not return when the asset is missing or broken
std::shared_ptr<engine::render::ITexture> tex =
    services.assets.get<engine::render::ITexture>(assets::textures::hero);

// 2. try_get<T>: returns std::expected<std::shared_ptr<T>, engine::AssetError>
auto maybe_sound = services.assets.try_get<engine::Sound>(assets::audio::sfx_jump);
if (maybe_sound) {
    services.audio.play_sfx(**maybe_sound);
} else {
    engine::log::warn("Sound failed to load: " + std::string(engine::to_string(maybe_sound.error())));
}
```

Use `get` for content the game cannot run without, and `try_get` for optional content.

Named sub-sprites of a sprite sheet come back as a ready `render::Sprite`: `services.assets.get_sprite(id, "name")` (`try_get_sprite` returns the error instead). See [Sprites & Animation](../rendering/Sprites-and-Animation.md#2-sprite-sheets-and-atlases).

---

## 2. Asset Errors

`try_get<T>` returns `std::expected<std::shared_ptr<T>, engine::AssetError>`. Possible errors:
- `AssetError::NotFound`: The GUID does not exist in any loaded catalog.
- `AssetError::Corrupt`: The file on disk cannot be read, parsed, or decoded.
- `AssetError::TypeMismatch`: The asset was requested with the wrong C++ type (e.g. asking for a `Sound` with a texture `AssetId`). See the type table in [Asset Pipeline](Asset-Pipeline.md#2-supported-importers).
- `AssetError::NotReady`: A GPU type (texture, mesh, shader) was requested before the engine had a graphic factory. A game that loads in `on_start` or later does not see this.

---

## 3. Automatic Caching

`AssetsDb` maintains an internal cache keyed by `(AssetId, type)`. Repeated calls to `get<T>()` return shared pointers to the previously loaded resource without touching disk I/O. The same `AssetId` requested as two different types is two separate loads.

---

## 4. Serializable objects

A `struct WIND_SERIALIZABLE` is a value the engine can read and write. It is not a component. The build writes `encode_wind` / `decode_wind` and `assets::register_game_objects` into `component_io.h`.

Three ways to use one struct:

1. Load it as an asset. Put a TOML table in `items/sword.object` with `type` set to the struct's key (its snake_case name, or the string in `WIND_SERIALIZABLE("item_drop")`) and the struct's fields. `asset_guid` sets `importer = "object"`. `services.assets.get<game::ItemDrop>(assets::items::sword)` returns it.
2. Point at that asset from a component with `engine::AssetId`. The `.world` file stores the guid. The engine does not load the object while it reads the component.
3. Nest the struct (or a `std::vector` of it) in a `WIND_COMPONENT`. Those fields are written into the same `.world` file and are not a separate asset. A nested value does not write `type`.

A static game calls `assets::register_game_objects(services.assets)` from its constructor. A game module registers on `wind_create_game`, and Edit Mode registers through `wind_register_game_objects`. `TextAsset` (`importer = "data"`) stays the path for a file the game parses itself.

---

## Next Steps

- Localize text tables in [Localization](Localization.md).
- Compile for target devices in [Desktop](../platforms/Desktop.md).
