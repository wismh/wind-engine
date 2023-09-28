#include <engine/render/graphics.h>
#include <engine/resources/assets_db.h>

#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <utility>

namespace engine {
namespace {

class StubTexture final : public render::ITexture {};

std::optional<ImporterKind> importer_for_type(const std::type_info& type) {
    if (type == typeid(render::ITexture)) {
        return ImporterKind::Texture;
    }
    return std::nullopt;
}

std::shared_ptr<void> make_stub(const std::type_info& type) {
    if (type == typeid(render::ITexture)) {
        std::shared_ptr<render::ITexture> texture = std::make_shared<StubTexture>();
        return std::static_pointer_cast<void>(std::move(texture));
    }
    return {};
}

}

AssetsDb::AssetsDb(IFatalError& fatal_error)
    : fatal_error_(fatal_error) {}

void AssetsDb::set_catalog(CookedCatalog catalog) {
    catalog_ = std::move(catalog);
    cache_.clear();
}

std::expected<std::shared_ptr<void>, AssetError> AssetsDb::try_get_erased(AssetId id, const std::type_info& type) {
    const CatalogEntry* const entry = catalog_.find(id);
    if (entry == nullptr) {
        return std::unexpected(AssetError::NotFound);
    }

    const auto wanted = importer_for_type(type);
    if (!wanted || *wanted != entry->importer) {
        return std::unexpected(AssetError::TypeMismatch);
    }

    const CacheKey key{id, std::type_index(type)};
    if (const auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    std::shared_ptr<void> handle = make_stub(type);
    if (!handle) {
        return std::unexpected(AssetError::Corrupt);
    }
    cache_.emplace(key, handle);
    return handle;
}

[[noreturn]] void AssetsDb::fail_get(AssetId id, AssetError error) {
    const std::string message = std::format("Asset {} error: {}", id.hex(), to_string(error));
    fatal_error_.report(message);
    std::abort();
}

}
