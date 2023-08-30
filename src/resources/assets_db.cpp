#include <engine/audio/sound.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/font.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include "audio/clip.h"
#include "render/material_instance.h"
#include "resources/importers.h"

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

bool needs_factory(const std::type_info& type) {
    return is_gpu_type(type) || type == typeid(render::IMaterial);
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
    if (type == typeid(render::IMaterial)) {
        return ImporterKind::Material;
    }
    if (type == typeid(Font)) {
        return ImporterKind::Font;
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

bool importer_matches(const std::type_info& type, ImporterKind actual) {
    if (type == typeid(render::ITexture)) {
        return actual == ImporterKind::Texture || actual == ImporterKind::UiImage;
    }
    const auto wanted = importer_for_type(type);
    return wanted.has_value() && *wanted == actual;
}

std::optional<std::string> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path lookup_path(const CatalogEntry& entry, const std::filesystem::path& fallback_root) {
    const std::filesystem::path& root = entry.files_root.empty() ? fallback_root : entry.files_root;
    return root / entry.relative_path;
}

std::expected<std::shared_ptr<void>, AssetError> load_gpu(const CatalogEntry& entry, const std::type_info& type,
        const std::filesystem::path& fallback_root, render::IGraphicFactory& factory) {
    const std::filesystem::path path = lookup_path(entry, fallback_root);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(AssetError::Corrupt);
    }
    const auto bytes = read_all(path);
    if (!bytes) {
        return std::unexpected(AssetError::Corrupt);
    }

    if (type == typeid(render::IMesh)) {
        const auto desc = parse_mesh(*bytes);
        if (!desc) {
            return std::unexpected(AssetError::Corrupt);
        }
        std::shared_ptr<render::IMesh> mesh = factory.create_mesh(*desc);
        if (!mesh) {
            return std::unexpected(AssetError::Corrupt);
        }
        return std::static_pointer_cast<void>(std::move(mesh));
    }

    if (type == typeid(render::IShader)) {
        const auto desc = parse_shader_xml(*bytes);
        if (!desc) {
            return std::unexpected(AssetError::Corrupt);
        }
        std::shared_ptr<render::IShader> shader = factory.create_shader(*desc);
        if (!shader) {
            return std::unexpected(AssetError::Corrupt);
        }
        return std::static_pointer_cast<void>(std::move(shader));
    }

    if (type == typeid(render::ITexture)) {
        auto desc = decode_png_rgba(*bytes);
        if (!desc) {
            return std::unexpected(AssetError::Corrupt);
        }
        desc->filter = entry.texture.filter;
        desc->wrap = entry.texture.wrap;
        std::shared_ptr<render::ITexture> texture = factory.create_texture(*desc);
        if (!texture) {
            return std::unexpected(AssetError::Corrupt);
        }
        return std::static_pointer_cast<void>(std::move(texture));
    }

    return std::unexpected(AssetError::Corrupt);
}

std::expected<std::shared_ptr<void>, AssetError> load_cpu(
        const CatalogEntry& entry, const std::type_info& type, const std::filesystem::path& fallback_root) {
    const std::filesystem::path path = lookup_path(entry, fallback_root);
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
        sound->pitch_range = {entry.audio.pitch_min, entry.audio.pitch_max};
        sound->loop = entry.audio.loop;
        sound->bank = entry.audio.bank;
        return std::static_pointer_cast<void>(std::move(sound));
    }

    if (type == typeid(Font)) {
        const auto bytes = read_all(path);
        if (!bytes || bytes->empty()) {
            return std::unexpected(AssetError::Corrupt);
        }
        auto font = std::make_shared<Font>();
        font->bytes.assign(bytes->begin(), bytes->end());
        return std::static_pointer_cast<void>(std::move(font));
    }

    return std::unexpected(AssetError::Corrupt);
}

std::expected<std::shared_ptr<void>, AssetError> load_material(
        AssetsDb& db, const CatalogEntry& entry, const std::filesystem::path& fallback_root) {
    const std::filesystem::path path = lookup_path(entry, fallback_root);
    const auto bytes = read_all(path);
    if (!bytes) {
        return std::unexpected(AssetError::Corrupt);
    }
    const auto desc = render::parse_material(*bytes);
    if (!desc) {
        return std::unexpected(AssetError::Corrupt);
    }

    const auto shader_id = AssetId::parse(desc->shader);
    if (!shader_id) {
        return std::unexpected(AssetError::Corrupt);
    }
    auto shader = db.try_get<render::IShader>(*shader_id);
    if (!shader) {
        return std::unexpected(shader.error());
    }

    std::shared_ptr<render::ITexture> albedo;
    if (!desc->albedo.empty()) {
        const auto albedo_id = AssetId::parse(desc->albedo);
        if (!albedo_id) {
            return std::unexpected(AssetError::Corrupt);
        }
        auto texture = db.try_get<render::ITexture>(*albedo_id);
        if (!texture) {
            return std::unexpected(texture.error());
        }
        albedo = std::move(*texture);
    }

    auto material = std::make_shared<render::Material>(std::move(*shader), std::move(albedo), desc->color, desc->blend);
    return std::static_pointer_cast<void>(std::move(material));
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

std::expected<void, MetaError> AssetsDb::load_catalog(
        const std::filesystem::path& catalog_file, const std::filesystem::path& files_root) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(catalog_file, ec)) {
        return std::unexpected(MetaError::Io);
    }
    const auto bytes = read_all(catalog_file);
    if (!bytes) {
        return std::unexpected(MetaError::Io);
    }
    auto parsed = parse_cooked_catalog(*bytes);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    CookedCatalog rooted;
    for (const CatalogEntry& entry : parsed->entries()) {
        CatalogEntry copy = entry;
        copy.files_root = files_root;
        rooted.add(std::move(copy));
    }
    add_catalog(std::move(rooted));
    return {};
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

    if (!importer_matches(type, entry->importer)) {
        return std::unexpected(AssetError::TypeMismatch);
    }

    const CacheKey key{id, std::type_index(type)};
    if (const auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    if (needs_factory(type) && graphic_factory_ == nullptr) {
        return std::unexpected(AssetError::NotReady);
    }

    std::expected<std::shared_ptr<void>, AssetError> loaded;
    if (is_gpu_type(type)) {
        loaded = load_gpu(*entry, type, assets_root_, *graphic_factory_);
    } else if (type == typeid(render::IMaterial)) {
        loaded = load_material(*this, *entry, assets_root_);
    } else {
        loaded = load_cpu(*entry, type, assets_root_);
    }
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
