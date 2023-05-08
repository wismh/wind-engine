#include <engine/resources/meta.h>

#include <toml++/toml.hpp>

#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {
namespace {

std::optional<float> node_as_float(const toml::node& node) {
    if (const auto f = node.value<double>()) {
        return static_cast<float>(*f);
    }
    if (const auto i = node.value<std::int64_t>()) {
        return static_cast<float>(*i);
    }
    return std::nullopt;
}

std::optional<ImporterKind> parse_importer(std::string_view value) {
    if (value == "texture") {
        return ImporterKind::Texture;
    }
    if (value == "audio") {
        return ImporterKind::Audio;
    }
    if (value == "mesh") {
        return ImporterKind::Mesh;
    }
    if (value == "shader") {
        return ImporterKind::Shader;
    }
    if (value == "font") {
        return ImporterKind::Font;
    }
    if (value == "ui_image") {
        return ImporterKind::UiImage;
    }
    if (value == "material") {
        return ImporterKind::Material;
    }
    if (value == "ui") {
        return ImporterKind::Ui;
    }
    if (value == "css") {
        return ImporterKind::Css;
    }
    return std::nullopt;
}

std::optional<ColorSpace> parse_color_space(std::string_view value) {
    if (value == "srgb") {
        return ColorSpace::Srgb;
    }
    if (value == "linear") {
        return ColorSpace::Linear;
    }
    return std::nullopt;
}

std::optional<FilterMode> parse_filter(std::string_view value) {
    if (value == "nearest") {
        return FilterMode::Nearest;
    }
    if (value == "linear") {
        return FilterMode::Linear;
    }
    return std::nullopt;
}

std::optional<WrapMode> parse_wrap(std::string_view value) {
    if (value == "clamp") {
        return WrapMode::Clamp;
    }
    if (value == "repeat") {
        return WrapMode::Repeat;
    }
    if (value == "mirror") {
        return WrapMode::Mirror;
    }
    return std::nullopt;
}

std::optional<TextureLayout> parse_layout(std::string_view value) {
    if (value == "single") {
        return TextureLayout::Single;
    }
    if (value == "multiple") {
        return TextureLayout::Multiple;
    }
    return std::nullopt;
}

std::optional<AudioBank> parse_bank(std::string_view value) {
    if (value == "sfx") {
        return AudioBank::Sfx;
    }
    if (value == "music") {
        return AudioBank::Music;
    }
    return std::nullopt;
}

std::expected<void, MetaError> apply_optional_audio(const toml::table& table, AudioImportSettings& audio) {
    if (const auto bank = table["bank"].value<std::string>()) {
        const auto parsed = parse_bank(*bank);
        if (!parsed) {
            return std::unexpected(MetaError::InvalidField);
        }
        audio.bank = *parsed;
    }
    if (const toml::node* volume_node = table.get("volume")) {
        const auto volume = node_as_float(*volume_node);
        if (!volume) {
            return std::unexpected(MetaError::InvalidField);
        }
        audio.volume = *volume;
    }
    if (const toml::node* pitch_node = table.get("pitch_range")) {
        const toml::array* const arr = pitch_node->as_array();
        if (arr == nullptr || arr->size() != 2) {
            return std::unexpected(MetaError::InvalidField);
        }
        const toml::node* const min_node = arr->get(0);
        const toml::node* const max_node = arr->get(1);
        if (min_node == nullptr || max_node == nullptr) {
            return std::unexpected(MetaError::InvalidField);
        }
        const auto pitch_min = node_as_float(*min_node);
        const auto pitch_max = node_as_float(*max_node);
        if (!pitch_min || !pitch_max) {
            return std::unexpected(MetaError::InvalidField);
        }
        audio.pitch_min = *pitch_min;
        audio.pitch_max = *pitch_max;
    }
    if (const auto loop = table["loop"].value<bool>()) {
        audio.loop = *loop;
    }
    return {};
}

std::string_view bank_to_string(AudioBank bank) noexcept {
    switch (bank) {
        case AudioBank::Sfx:
            return "sfx";
        case AudioBank::Music:
            return "music";
    }
    return "sfx";
}

std::expected<AssetMeta, MetaError> parse_meta_table(const toml::table& table) {
    if (!table.contains("guid")) {
        return std::unexpected(MetaError::MissingGuid);
    }
    if (!table.contains("importer")) {
        return std::unexpected(MetaError::MissingImporter);
    }

    const auto guid_text = table["guid"].value<std::string>();
    if (!guid_text) {
        return std::unexpected(MetaError::InvalidGuid);
    }
    auto guid = AssetId::parse(*guid_text);
    if (!guid) {
        return std::unexpected(MetaError::InvalidGuid);
    }

    const auto importer_text = table["importer"].value<std::string>();
    if (!importer_text) {
        return std::unexpected(MetaError::UnknownImporter);
    }
    const auto importer = parse_importer(*importer_text);
    if (!importer) {
        return std::unexpected(MetaError::UnknownImporter);
    }

    AssetMeta meta;
    meta.guid = *guid;
    meta.importer = *importer;

    if (const auto color_space = table["color_space"].value<std::string>()) {
        const auto parsed = parse_color_space(*color_space);
        if (!parsed) {
            return std::unexpected(MetaError::InvalidField);
        }
        meta.texture.color_space = *parsed;
    }
    if (const auto filter = table["filter"].value<std::string>()) {
        const auto parsed = parse_filter(*filter);
        if (!parsed) {
            return std::unexpected(MetaError::InvalidField);
        }
        meta.texture.filter = *parsed;
    }
    if (const auto wrap = table["wrap"].value<std::string>()) {
        const auto parsed = parse_wrap(*wrap);
        if (!parsed) {
            return std::unexpected(MetaError::InvalidField);
        }
        meta.texture.wrap = *parsed;
    }
    if (const auto layout = table["layout"].value<std::string>()) {
        const auto parsed = parse_layout(*layout);
        if (!parsed) {
            return std::unexpected(MetaError::InvalidField);
        }
        meta.texture.layout = *parsed;
    }

    if (auto audio = apply_optional_audio(table, meta.audio); !audio) {
        return std::unexpected(audio.error());
    }

    return meta;
}

std::string to_identifier(std::string_view piece) {
    std::string out;
    out.reserve(piece.size());
    for (char c : piece) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

}

std::string_view to_string(ImporterKind kind) noexcept {
    switch (kind) {
        case ImporterKind::Texture:
            return "texture";
        case ImporterKind::Audio:
            return "audio";
        case ImporterKind::Mesh:
            return "mesh";
        case ImporterKind::Shader:
            return "shader";
        case ImporterKind::Font:
            return "font";
        case ImporterKind::UiImage:
            return "ui_image";
        case ImporterKind::Material:
            return "material";
        case ImporterKind::Ui:
            return "ui";
        case ImporterKind::Css:
            return "css";
    }
    return "texture";
}

std::expected<AssetMeta, MetaError> parse_asset_meta(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        return parse_meta_table(table);
    } catch (const toml::parse_error&) {
        return std::unexpected(MetaError::InvalidToml);
    }
}

void CookedCatalog::add(CatalogEntry entry) {
    entries_.push_back(std::move(entry));
}

const CatalogEntry* CookedCatalog::find(AssetId id) const {
    for (const CatalogEntry& entry : entries_) {
        if (entry.guid == id) {
            return &entry;
        }
    }
    return nullptr;
}

const std::vector<CatalogEntry>& CookedCatalog::entries() const noexcept {
    return entries_;
}

std::string CookedCatalog::serialize() const {
    std::ostringstream os;
    for (const CatalogEntry& entry : entries_) {
        os << "[[assets]]\n";
        os << "guid = \"" << entry.guid.hex() << "\"\n";
        os << "path = \"" << entry.relative_path << "\"\n";
        os << "importer = \"" << to_string(entry.importer) << "\"\n";
        if (entry.importer == ImporterKind::Audio) {
            os << "bank = \"" << bank_to_string(entry.audio.bank) << "\"\n";
            os << "volume = " << entry.audio.volume << "\n";
            os << "pitch_range = [" << entry.audio.pitch_min << ", " << entry.audio.pitch_max << "]\n";
            os << "loop = " << (entry.audio.loop ? "true" : "false") << "\n";
        }
        os << '\n';
    }
    return os.str();
}

std::expected<CookedCatalog, MetaError> parse_cooked_catalog(std::string_view toml_text) {
    try {
        const toml::table table = toml::parse(toml_text);
        const toml::node* assets_node = table.get("assets");
        if (assets_node == nullptr) {
            return CookedCatalog{};
        }
        const toml::array* const assets = assets_node->as_array();
        if (assets == nullptr) {
            return std::unexpected(MetaError::InvalidField);
        }

        CookedCatalog catalog;
        for (const toml::node& node : *assets) {
            const toml::table* const entry_table = node.as_table();
            if (entry_table == nullptr) {
                return std::unexpected(MetaError::InvalidField);
            }
            const auto guid_text = (*entry_table)["guid"].value<std::string>();
            const auto path = (*entry_table)["path"].value<std::string>();
            const auto importer_text = (*entry_table)["importer"].value<std::string>();
            if (!guid_text || !path || !importer_text) {
                return std::unexpected(MetaError::InvalidField);
            }
            auto guid = AssetId::parse(*guid_text);
            if (!guid) {
                return std::unexpected(MetaError::InvalidGuid);
            }
            const auto importer = parse_importer(*importer_text);
            if (!importer) {
                return std::unexpected(MetaError::UnknownImporter);
            }
            CatalogEntry cooked{*guid, *path, *importer};
            if (auto audio = apply_optional_audio(*entry_table, cooked.audio); !audio) {
                return std::unexpected(audio.error());
            }
            catalog.add(std::move(cooked));
        }
        return catalog;
    } catch (const toml::parse_error&) {
        return std::unexpected(MetaError::InvalidToml);
    }
}

AssetCppId identifier_from_path(std::string_view relative_path) {
    std::string normalized(relative_path);
    for (char& c : normalized) {
        if (c == '\\') {
            c = '/';
        }
    }

    AssetCppId id;
    id.namespaces.push_back("assets");

    std::string current;
    std::vector<std::string> parts;
    for (char c : normalized) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }

    if (parts.empty()) {
        id.name = "_";
        return id;
    }

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        id.namespaces.push_back(to_identifier(parts[i]));
    }

    std::string filename = parts.back();
    const auto dot = filename.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        filename.resize(dot);
    }
    id.name = to_identifier(filename);
    return id;
}

std::string AssetCppId::qualified() const {
    std::string out;
    for (const std::string& ns : namespaces) {
        out += ns;
        out += "::";
    }
    out += name;
    return out;
}

}
