# Loading Assets

Wind loads and caches resources through `engine::AssetsDb`. Game code retrieves assets strictly using compile-time `AssetId` handles—never raw string file paths.

---

## 1. Type-Safe Asset Access

`AssetsDb` provides two retrieval methods:

```cpp
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

// 1. Unchecked get<T> (Throws fatal error if missing or corrupted)
std::shared_ptr<engine::render::ITexture> tex =
    services.assets.get<engine::render::ITexture>(assets::textures::hero);

// 2. Safe try_get<T> (Returns std::expected)
auto maybe_sound = services.assets.try_get<engine::Sound>(assets::audio::sfx_jump);
if (maybe_sound) {
    services.audio.play_sfx(**maybe_sound);
} else {
    engine::log::warn("Sound failed to load: {}", engine::to_string(maybe_sound.error()));
}
```

---

## 2. Asset Errors

`try_get<T>` returns `std::expected<std::shared_ptr<T>, engine::AssetError>`. Possible errors include:
- `AssetError::NotFound`: The GUID does not exist in any loaded catalog.
- `AssetError::Corrupt`: File on disk cannot be parsed or decoded.
- `AssetError::TypeMismatch`: The asset was requested with the wrong C++ type (e.g. asking for a `Sound` with a texture `AssetId`).
- `AssetError::NotReady`: Resource is still streaming (for asynchronous loaders).

---

## 3. Automatic Caching

`AssetsDb` maintains an internal cache keyed by `(AssetId, type)`. Repeated calls to `get<T>()` return shared pointers to the previously loaded resource without touching disk I/O.

---

## Next Steps

- Localize text tables in [Localization](Localization.md).
- Compile for target devices in [Desktop](../platforms/Desktop.md).
