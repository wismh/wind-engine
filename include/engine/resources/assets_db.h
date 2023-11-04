#pragma once

#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
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

class AssetsDb {
public:
    explicit AssetsDb(IFatalError& fatal_error);

    void set_catalog(CookedCatalog catalog);
    void add_catalog(CookedCatalog catalog);
    void set_root(std::filesystem::path assets_root);
    void set_graphic_factory(render::IGraphicFactory* factory);

    template<typename T>
    [[nodiscard]] std::expected<std::shared_ptr<T>, AssetError> TryGet(AssetId id);

    template<typename T>
    [[nodiscard]] std::shared_ptr<T> Get(AssetId id);

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

    IFatalError& fatal_error_;
    CookedCatalog catalog_;
    std::filesystem::path assets_root_;
    render::IGraphicFactory* graphic_factory_ = nullptr;
    std::unordered_map<CacheKey, std::shared_ptr<void>, CacheKeyHash> cache_;
};

template<typename T>
std::expected<std::shared_ptr<T>, AssetError> AssetsDb::TryGet(AssetId id) {
    auto result = try_get_erased(id, typeid(T));
    if (!result) {
        return std::unexpected(result.error());
    }
    return std::static_pointer_cast<T>(*result);
}

template<typename T>
std::shared_ptr<T> AssetsDb::Get(AssetId id) {
    auto result = TryGet<T>(id);
    if (result) {
        return std::move(*result);
    }
    fail_get(id, result.error());
}

}
