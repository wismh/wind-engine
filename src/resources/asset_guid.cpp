#include <engine/resources/asset_guid.h>

#include <engine/resources/meta.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

bool is_meta_file(const std::filesystem::path& path) {
    return path.extension() == ".meta";
}

bool is_skippable_file(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (name.empty() || name.front() == '.') {
        return true;
    }

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "readme.md" || lower == "license" || lower == "copying" || lower == "ofl.txt") {
        return true;
    }
    if (lower.starts_with("license.")) {
        return true;
    }
    return path.extension() == ".md";
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<ImporterKind> importer_from_extension(const std::filesystem::path& path) {
    const std::string ext = to_lower(path.extension().string());
    if (ext == ".png") {
        return ImporterKind::Texture;
    }
    if (ext == ".wav") {
        return ImporterKind::Audio;
    }
    if (ext == ".xml") {
        return ImporterKind::Ui;
    }
    if (ext == ".css") {
        return ImporterKind::Css;
    }
    if (ext == ".mat") {
        return ImporterKind::Material;
    }
    if (ext == ".mesh") {
        return ImporterKind::Mesh;
    }
    if (ext == ".shader") {
        return ImporterKind::Shader;
    }
    if (ext == ".ttf") {
        return ImporterKind::Font;
    }
    return std::nullopt;
}

std::string random_guid(std::mt19937& rng, const std::unordered_set<std::string>& used) {
    std::uniform_int_distribution<int> dist(0, 15);
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex(AssetId::kHexLength, '0');
    for (;;) {
        for (char& c : hex) {
            c = kDigits[dist(rng)];
        }
        if (!used.contains(hex)) {
            return hex;
        }
    }
}

std::string default_meta_text(std::string_view guid, ImporterKind importer) {
    std::string text;
    text += "guid = \"";
    text += guid;
    text += "\"\nimporter = \"";
    text += to_string(importer);
    text += "\"\n";
    if (importer == ImporterKind::Audio) {
        text += "bank = \"sfx\"\n";
        text += "volume = 1.0\n";
        text += "pitch_range = [1.0, 1.0]\n";
        text += "loop = false\n";
    }
    return text;
}

void remember_existing_guid(const std::filesystem::path& meta_path, std::unordered_set<std::string>& used) {
    std::ifstream in(meta_path);
    if (!in) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = parse_asset_meta(text);
    if (parsed) {
        used.emplace(parsed->guid.hex());
    }
}

}

int write_missing_metas(const std::filesystem::path& root) {
    std::error_code exists_ec;
    if (!std::filesystem::exists(root, exists_ec) || !std::filesystem::is_directory(root)) {
        return 0;
    }

    std::unordered_set<std::string> used;
    std::mt19937 rng{std::random_device{}()};
    int written = 0;

    std::error_code iter_ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, options, iter_ec)) {
        if (iter_ec || !entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& file = entry.path();
        if (is_meta_file(file) || is_skippable_file(file)) {
            continue;
        }
        const auto importer = importer_from_extension(file);
        if (!importer) {
            continue;
        }

        std::filesystem::path meta_path = file;
        meta_path += ".meta";
        if (std::filesystem::exists(meta_path)) {
            remember_existing_guid(meta_path, used);
            continue;
        }

        const std::string guid = random_guid(rng, used);
        used.insert(guid);

        std::ofstream out(meta_path, std::ios::trunc);
        if (!out) {
            continue;
        }
        out << default_meta_text(guid, *importer);
        if (!out) {
            continue;
        }
        ++written;
    }
    return written;
}

}
