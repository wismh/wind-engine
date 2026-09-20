#pragma once

// docs/tech/features/Assets.md

#include <engine/ecs/world_io.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/sprite.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace engine {

namespace render {
class IGraphicFactory;
}

enum class AssetError {
    NotFound,
    Corrupt,
    TypeMismatch,
    NotReady,
};

[[nodiscard]] constexpr std::string_view to_string(AssetError error) noexcept {
    switch (error) {
        case AssetError::NotFound:
            return "NotFound";
        case AssetError::Corrupt:
            return "Corrupt";
        case AssetError::TypeMismatch:
            return "TypeMismatch";
        case AssetError::NotReady:
            return "NotReady";
    }
    return "Unknown";
}

// Builds a `T` from an object file's field table. Instantiated in the game module so `T`'s deleter lives there.
using ObjectLoadFn = std::function<std::expected<std::shared_ptr<void>, AssetError>(const ecs::FieldTable&)>;

class AssetsDb {
public:
    explicit AssetsDb(IFatalError& fatal_error);

    void set_catalog(CookedCatalog catalog);
    void add_catalog(CookedCatalog catalog);
    [[nodiscard]] std::expected<void, MetaError> load_catalog(const std::filesystem::path& catalog_file,
            const std::filesystem::path& files_root);
    // Removes the entries `load_catalog` added with this files root and evicts their cached assets.
    // Entries from other roots (engine builtins) and their cache stay.
    void unload_catalog(const std::filesystem::path& files_root);
    void set_root(std::filesystem::path assets_root);
    void set_graphic_factory(render::IGraphicFactory* factory);
    [[nodiscard]] render::IGraphicFactory* graphic_factory() const noexcept { return graphic_factory_; }

    [[nodiscard]] const CookedCatalog& catalog() const noexcept { return catalog_; }

    template<typename T>
    [[nodiscard]] std::expected<std::shared_ptr<T>, AssetError> try_get(AssetId id);

    template<typename T>
    [[nodiscard]] std::shared_ptr<T> get(AssetId id);

    // A UI image made at runtime, such as the editor's thumbnails. `id` is not in the catalog; the UI looks here
    // first. Replaces an image already set for `id`. A window that registered the old pixels keeps them until its
    // UI cache is reset. Main thread only.
    void set_runtime_texture(AssetId id, render::TextureDesc desc);
    [[nodiscard]] const render::TextureDesc* runtime_texture(AssetId id) const;
    void clear_runtime_textures();

    [[nodiscard]] std::expected<render::Sprite, AssetError> try_get_sprite(AssetId id, std::string_view name = "");
    [[nodiscard]] render::Sprite get_sprite(AssetId id, std::string_view name = "");

    // `key` is the `.object` file's `type` string. `read` fills `T` from the file's table. Replaces a previous
    // loader for `T`. The function must not outlive this registration: call `clear_object_loaders` before the
    // module that compiled `read` is unloaded.
    template<typename T, typename ReadFn>
    void register_object(std::string_view key, ReadFn&& read) {
        register_object_erased(typeid(T), std::string(key),
                               [read = std::forward<ReadFn>(read)](
                                   const ecs::FieldTable& table) -> std::expected<std::shared_ptr<void>, AssetError> {
                                   T value{};
                                   ecs::ComponentReader reader(table);
                                   read(reader, value);
                                   if (!reader.ok()) {
                                       return std::unexpected(AssetError::Corrupt);
                                   }
                                   return std::static_pointer_cast<void>(std::make_shared<T>(std::move(value)));
                               });
    }

    // Drops every object loader and evicts cached values those loaders produced. Call while the module that
    // registered them is still mapped, so their deleters run.
    void clear_object_loaders();

private:
    struct CacheKey {
        AssetId id;
        std::type_index type;

        bool operator==(const CacheKey&) const = default;
    };

    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const noexcept {
            const std::size_t guid_hash = std::hash<std::string_view>{}(key.id.hex());
            return guid_hash ^ (key.type.hash_code() + 0x9e3779b9 + (guid_hash << 6) + (guid_hash >> 2));
        }
    };

    [[nodiscard]] std::expected<std::shared_ptr<void>, AssetError> try_get_erased(AssetId id, const std::type_info& type);
    [[noreturn]] void fail_get(AssetId id, AssetError error);
    void register_object_erased(const std::type_info& type, std::string key, ObjectLoadFn load);
    [[nodiscard]] std::expected<std::shared_ptr<void>, AssetError> load_object(const CatalogEntry& entry,
                                                                               const std::type_info& type);

    IFatalError& fatal_error_;
    CookedCatalog catalog_;
    std::filesystem::path assets_root_;
    render::IGraphicFactory* graphic_factory_ = nullptr;
    struct ObjectLoader {
        std::string key;
        ObjectLoadFn load;
    };

    std::unordered_map<CacheKey, std::shared_ptr<void>, CacheKeyHash> cache_;
    std::unordered_map<std::type_index, ObjectLoader> object_loaders_;
    struct IdHash {
        std::size_t operator()(const AssetId& id) const noexcept { return std::hash<std::string_view>{}(id.hex()); }
    };
    std::unordered_map<AssetId, render::TextureDesc, IdHash> runtime_textures_;
};

template<typename T>
std::expected<std::shared_ptr<T>, AssetError> AssetsDb::try_get(AssetId id) {
    auto result = try_get_erased(id, typeid(T));
    if (!result) {
        return std::unexpected(result.error());
    }
    return std::static_pointer_cast<T>(*result);
}

template<typename T>
std::shared_ptr<T> AssetsDb::get(AssetId id) {
    auto result = try_get<T>(id);
    if (result) {
        return std::move(*result);
    }
    fail_get(id, result.error());
}

}
