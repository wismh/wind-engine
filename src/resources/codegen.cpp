#include <engine/resources/meta.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

std::string generic_relative(const std::filesystem::path& root, const std::filesystem::path& file) {
    return file.lexically_relative(root).generic_string();
}

std::expected<void, CodegenError> emit_ids_header(const std::vector<std::pair<AssetCppId, AssetId>>& ids,
        std::string& out) {
    std::ostringstream os;
    os << "#pragma once\n\n";
    os << "#include <engine/resources/asset_id.h>\n\n";

    for (const auto& [cpp_id, guid] : ids) {
        for (const std::string& ns : cpp_id.namespaces) {
            os << "namespace " << ns << " {\n";
        }
        os << "inline constexpr engine::AssetId " << cpp_id.name << "{\"" << guid.hex() << "\"};\n";
        for (std::size_t i = 0; i < cpp_id.namespaces.size(); ++i) {
            os << "}\n";
        }
        os << '\n';
    }

    out = os.str();
    return {};
}

}

std::expected<CodegenOutput, CodegenError> codegen_scan(const std::filesystem::path& assets_root,
        std::span<const AssetId> reserved) {
    std::error_code exists_ec;
    if (!std::filesystem::exists(assets_root, exists_ec) || !std::filesystem::is_directory(assets_root)) {
        return std::unexpected(CodegenError{CodegenErrorKind::Io, "assets root is not a directory"});
    }

    CodegenOutput output;
    std::unordered_map<std::string, std::string> guid_to_path;
    std::unordered_set<std::string> reserved_hex;
    reserved_hex.reserve(reserved.size());
    for (const AssetId& id : reserved) {
        reserved_hex.emplace(id.hex());
    }
    std::vector<std::pair<AssetCppId, AssetId>> ids;

    std::error_code iter_ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_root, options, iter_ec)) {
        if (iter_ec) {
            return std::unexpected(CodegenError{CodegenErrorKind::Io, iter_ec.message()});
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& file = entry.path();
        if (is_meta_file(file) || is_skippable_file(file)) {
            continue;
        }

        std::filesystem::path meta_path = file;
        meta_path += ".meta";
        if (!std::filesystem::exists(meta_path)) {
            const std::string relative = generic_relative(assets_root, file);
            return std::unexpected(CodegenError{CodegenErrorKind::MissingMeta, "missing .meta for " + relative});
        }

        std::ifstream in(meta_path);
        if (!in) {
            return std::unexpected(CodegenError{CodegenErrorKind::Io, "failed to read " + meta_path.generic_string()});
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = parse_asset_meta(text);
        if (!parsed) {
            const CodegenErrorKind kind =
                    parsed.error() == MetaError::InvalidGuid ? CodegenErrorKind::InvalidGuid : CodegenErrorKind::InvalidMeta;
            return std::unexpected(CodegenError{kind, "invalid meta " + meta_path.generic_string()});
        }

        const std::string relative = generic_relative(assets_root, file);
        const std::string guid_hex(parsed->guid.hex());
        if (reserved_hex.contains(guid_hex)) {
            return std::unexpected(CodegenError{CodegenErrorKind::Collision,
                    "guid " + guid_hex + " is reserved by engine builtins (" + relative + ")"});
        }
        if (const auto it = guid_to_path.find(guid_hex); it != guid_to_path.end()) {
            return std::unexpected(CodegenError{CodegenErrorKind::Collision,
                    "guid " + guid_hex + " used by " + it->second + " and " + relative});
        }
        guid_to_path.emplace(guid_hex, relative);

        CatalogEntry catalog_entry;
        catalog_entry.guid = parsed->guid;
        catalog_entry.relative_path = relative;
        catalog_entry.importer = parsed->importer;
        catalog_entry.audio = parsed->audio;
        output.catalog.add(std::move(catalog_entry));
        ids.emplace_back(identifier_from_path(relative), parsed->guid);
    }

    std::sort(ids.begin(), ids.end(), [](const auto& a, const auto& b) { return a.first.qualified() < b.first.qualified(); });
    auto header = emit_ids_header(ids, output.asset_ids_header);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    return output;
}

std::expected<void, CodegenError> codegen_write(const std::filesystem::path& assets_root,
        const std::filesystem::path& output_dir, std::span<const AssetId> reserved) {
    auto scanned = codegen_scan(assets_root, reserved);
    if (!scanned) {
        return std::unexpected(std::move(scanned.error()));
    }

    std::error_code create_ec;
    std::filesystem::create_directories(output_dir, create_ec);
    if (create_ec) {
        return std::unexpected(CodegenError{CodegenErrorKind::Io, create_ec.message()});
    }

    const std::filesystem::path ids_path = output_dir / "asset_ids.h";
    std::ofstream ids_out(ids_path, std::ios::trunc);
    if (!ids_out) {
        return std::unexpected(CodegenError{CodegenErrorKind::Io, "failed to write " + ids_path.generic_string()});
    }
    ids_out << scanned->asset_ids_header;

    const std::filesystem::path catalog_path = output_dir / "catalog.toml";
    std::ofstream catalog_out(catalog_path, std::ios::trunc);
    if (!catalog_out) {
        return std::unexpected(CodegenError{CodegenErrorKind::Io, "failed to write " + catalog_path.generic_string()});
    }
    catalog_out << scanned->catalog.serialize();
    return {};
}

}
