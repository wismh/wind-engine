#include <engine/audio/sound.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/resources/assets_db.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include "audio/clip.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace engine {
namespace {

bool is_gpu_type(const std::type_info& type) {
    return type == typeid(render::ITexture) || type == typeid(render::IMesh) || type == typeid(render::IShader);
}

std::optional<ImporterKind> importer_for_type(const std::type_info& type) {
    if (type == typeid(render::ITexture)) {
        return ImporterKind::Texture;
    }
    if (type == typeid(render::IMesh)) {
        return ImporterKind::Mesh;
    }
    if (type == typeid(render::IShader)) {
        return ImporterKind::Shader;
    }
    if (type == typeid(ui::UiDocument)) {
        return ImporterKind::Ui;
    }
    if (type == typeid(ui::Stylesheet)) {
        return ImporterKind::Css;
    }
    if (type == typeid(Sound)) {
        return ImporterKind::Audio;
    }
    return std::nullopt;
}

std::optional<std::string> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::expected<std::shared_ptr<void>, AssetError> load_cpu(
        const CatalogEntry& entry, const std::type_info& type, const std::filesystem::path& root) {
    const std::filesystem::path path = root / entry.relative_path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(AssetError::Corrupt);
    }

    if (type == typeid(ui::UiDocument)) {
        const auto bytes = read_all(path);
        if (!bytes) {
            return std::unexpected(AssetError::Corrupt);
        }
        auto parsed = ui::parse_xml(*bytes, nullptr);
        if (!parsed) {
            return std::unexpected(AssetError::Corrupt);
        }
        std::shared_ptr<ui::UiDocument> document = std::make_shared<ui::UiDocument>(std::move(*parsed));
        return std::static_pointer_cast<void>(std::move(document));
    }

    if (type == typeid(ui::Stylesheet)) {
        const auto bytes = read_all(path);
        if (!bytes) {
            return std::unexpected(AssetError::Corrupt);
        }
        std::vector<std::string> warnings;
        auto parsed = ui::parse_css(*bytes, warnings);
        if (!parsed) {
            return std::unexpected(AssetError::Corrupt);
        }
        std::shared_ptr<ui::Stylesheet> sheet = std::make_shared<ui::Stylesheet>(std::move(*parsed));
        return std::static_pointer_cast<void>(std::move(sheet));
    }

    if (type == typeid(Sound)) {
        auto sound = std::make_shared<Sound>();
        sound->clip = std::make_shared<Audio>();
        const std::u8string utf8 = path.generic_u8string();
        audio_set_path(*sound->clip, std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
        sound->volume = entry.audio.volume;
        sound->pitchRange = {entry.audio.pitch_min, entry.audio.pitch_max};
        sound->loop = entry.audio.loop;
        sound->bank = entry.audio.bank;
        return std::static_pointer_cast<void>(std::move(sound));
    }

    return std::unexpected(AssetError::Corrupt);
}

}

AssetsDb::AssetsDb(IFatalError& fatal_error)
    : fatal_error_(fatal_error) {}

void AssetsDb::set_catalog(CookedCatalog catalog) {
    catalog_ = std::move(catalog);
    cache_.clear();
}

void AssetsDb::add_catalog(CookedCatalog catalog) {
    for (const CatalogEntry& entry : catalog.entries()) {
        catalog_.add(entry);
    }
    cache_.clear();
}

void AssetsDb::set_root(std::filesystem::path assets_root) {
    assets_root_ = std::move(assets_root);
    cache_.clear();
}

void AssetsDb::set_graphic_factory(render::IGraphicFactory* factory) {
    graphic_factory_ = factory;
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

    if (is_gpu_type(type)) {
        if (graphic_factory_ == nullptr) {
            return std::unexpected(AssetError::NotReady);
        }
        // Slice 13: GPU upload is not implemented yet (factory is reserved for later slices).
        return std::unexpected(AssetError::NotReady);
    }

    const CacheKey key{id, std::type_index(type)};
    if (const auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    auto loaded = load_cpu(*entry, type, assets_root_);
    if (!loaded) {
        return loaded;
    }
    cache_.emplace(key, *loaded);
    return loaded;
}

[[noreturn]] void AssetsDb::fail_get(AssetId id, AssetError error) {
    const std::string message = std::format("Asset {} error: {}", id.hex(), to_string(error));
    fatal_error_.report(message);
    std::abort();
}

}
