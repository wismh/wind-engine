#include "asset_inspection.h"

#include <engine/resources/meta.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace editor {
namespace {

constexpr std::size_t kTabWidth = 4;
constexpr std::array<unsigned char, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string size_text(std::uintmax_t bytes) {
    if (bytes < 1024) {
        return std::format("{} bytes", bytes);
    }
    constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    return std::format("{:.1f} {}", value, kUnits[unit]);
}

// Up to `limit` bytes from the start of the file; nullopt when it does not open.
std::optional<std::string> read_head(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string bytes(limit, '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(limit));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return bytes;
}

std::string_view meta_error_text(engine::MetaError error) {
    switch (error) {
    case engine::MetaError::InvalidToml:
        return "it is not TOML";
    case engine::MetaError::MissingGuid:
        return "it has no guid";
    case engine::MetaError::MissingImporter:
        return "it has no importer";
    case engine::MetaError::InvalidGuid:
        return "its guid is not 32 hex digits";
    case engine::MetaError::UnknownImporter:
        return "its importer is unknown";
    case engine::MetaError::InvalidField:
        return "a field has a wrong value";
    case engine::MetaError::Io:
        return "it does not open";
    }
    return "unknown error";
}

std::string_view color_space_text(engine::ColorSpace value) {
    return value == engine::ColorSpace::Srgb ? "srgb" : "linear";
}

std::string_view filter_text(engine::FilterMode value) {
    return value == engine::FilterMode::Nearest ? "nearest" : "linear";
}

std::string_view wrap_text(engine::WrapMode value) {
    switch (value) {
    case engine::WrapMode::Clamp:
        return "clamp";
    case engine::WrapMode::Repeat:
        return "repeat";
    case engine::WrapMode::Mirror:
        return "mirror";
    }
    return "clamp";
}

void add_texture_lines(const engine::TextureImportSettings& texture, std::vector<std::string>& lines) {
    lines.push_back(std::format("Color space: {}", color_space_text(texture.color_space)));
    lines.push_back(std::format("Filter: {}", filter_text(texture.filter)));
    lines.push_back(std::format("Wrap: {}", wrap_text(texture.wrap)));
    lines.push_back(std::format("Layout: {}", texture.layout == engine::TextureLayout::Single ? "single" : "multiple"));
    lines.push_back(std::format("Pixels per unit: {}", texture.pixels_per_unit));
    if (!texture.sprites.empty()) {
        lines.push_back(std::format("Sprites: {}", texture.sprites.size()));
    }
}

void add_audio_lines(const engine::AudioImportSettings& audio, std::vector<std::string>& lines) {
    lines.push_back(std::format("Bank: {}", audio.bank == engine::AudioBank::Sfx ? "sfx" : "music"));
    lines.push_back(std::format("Volume: {}", audio.volume));
    lines.push_back(std::format("Pitch: {} to {}", audio.pitch_min, audio.pitch_max));
    lines.push_back(std::format("Loop: {}", audio.loop ? "yes" : "no"));
}

std::optional<InspectorSection> import_section(const std::filesystem::path& file) {
    std::filesystem::path meta_path = file;
    meta_path += ".meta";
    std::error_code error;
    if (!std::filesystem::is_regular_file(meta_path, error)) {
        return std::nullopt;
    }
    InspectorSection section{.heading = "Import"};
    std::ifstream in(meta_path, std::ios::binary);
    if (!in) {
        section.lines.push_back(std::format("The .meta does not read: {}.", meta_error_text(engine::MetaError::Io)));
        return section;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::expected<engine::AssetMeta, engine::MetaError> meta = engine::parse_asset_meta(text);
    if (!meta) {
        section.lines.push_back(std::format("The .meta does not read: {}.", meta_error_text(meta.error())));
        return section;
    }
    section.lines.push_back(std::format("GUID: {}", meta->guid.hex()));
    section.lines.push_back(std::format("Importer: {}", engine::to_string(meta->importer)));
    switch (meta->importer) {
    case engine::ImporterKind::Texture:
    case engine::ImporterKind::UiImage:
        add_texture_lines(meta->texture, section.lines);
        break;
    case engine::ImporterKind::Audio:
        add_audio_lines(meta->audio, section.lines);
        break;
    case engine::ImporterKind::Strings:
        section.lines.push_back(std::format("Key source: {}", meta->strings_source ? "yes" : "no"));
        break;
    default:
        break;
    }
    return section;
}

std::uint32_t big_endian_u32(std::string_view bytes) {
    std::uint32_t value = 0;
    for (const char byte : bytes.substr(0, 4)) {
        value = (value << 8) | static_cast<unsigned char>(byte);
    }
    return value;
}

// Width and height from a PNG's IHDR chunk, which the format puts first.
std::optional<InspectorSection> png_section(std::string_view head) {
    constexpr std::size_t kIhdrEnd = 24;
    if (head.size() < kIhdrEnd || !std::equal(kPngSignature.begin(), kPngSignature.end(), head.begin(),
                                          [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); })) {
        return std::nullopt;
    }
    if (head.substr(12, 4) != "IHDR") {
        return std::nullopt;
    }
    return InspectorSection{.heading = "Content",
            .lines = {std::format("PNG image, {} x {}", big_endian_u32(head.substr(16)), big_endian_u32(head.substr(20)))}};
}

std::string expand_tabs(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (const char c : line) {
        if (c == '\t') {
            out.append(kTabWidth - out.size() % kTabWidth, ' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::optional<InspectorSection> text_section(std::string_view head, std::uintmax_t file_size) {
    if (head.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    InspectorSection section{.heading = "Content"};
    bool cut = head.size() < file_size;
    std::size_t start = 0;
    while (start < head.size()) {
        if (section.lines.size() == kMaxContentLines) {
            cut = true;
            break;
        }
        std::size_t end = head.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last) {
            end = head.size();
        }
        std::string_view line = head.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        // The last line of a cut read is a partial line: leave it to the note.
        if (last && cut) {
            break;
        }
        section.lines.push_back(expand_tabs(line));
        start = end + 1;
    }
    if (section.lines.empty() && !cut) {
        section.lines.push_back("Empty");
    }
    if (cut) {
        section.lines.push_back(std::format("... the first {} lines of {}", section.lines.size(), size_text(file_size)));
    }
    return section;
}

}

std::vector<InspectorSection> inspect_asset(const AssetSelection& asset) {
    std::vector<InspectorSection> sections;
    if (asset.directory) {
        sections.push_back(InspectorSection{.heading = "Folder",
                .lines = {std::format("Path: {}", asset.key),
                        std::format("Items: {}", asset.items)}});
        return sections;
    }
    sections.push_back(InspectorSection{.heading = "File",
            .lines = {std::format("Path: {}", asset.key), std::format("Size: {}", size_text(asset.size))}});
    if (std::optional<InspectorSection> import = import_section(asset.path)) {
        sections.push_back(std::move(*import));
    }
    const std::optional<std::string> head = read_head(asset.path, kMaxContentBytes);
    if (!head) {
        sections.front().lines.push_back(std::format("{} does not open.", path_text(asset.path.filename())));
        return sections;
    }
    if (std::optional<InspectorSection> png = png_section(*head)) {
        sections.push_back(std::move(*png));
    } else if (std::optional<InspectorSection> text = text_section(*head, asset.size)) {
        sections.push_back(std::move(*text));
    }
    return sections;
}

}
