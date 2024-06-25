#include <engine/resources/meta.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/document.h>

#include "ui/bind_scan.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
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

struct EmittedId {
    AssetCppId cpp_id;
    AssetId guid;
    std::optional<ui::BindBinder> ui_binder;
};

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

std::string to_pascal_case(std::string_view ident) {
    std::string out;
    bool cap = true;
    for (char c : ident) {
        if (c == '_') {
            cap = true;
            continue;
        }
        if (cap) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            cap = false;
        } else {
            out.push_back(c);
        }
    }
    return out.empty() ? "Item" : out;
}

std::string binder_struct_name(std::string_view stem) {
    return to_pascal_case(to_identifier(stem));
}

std::string nested_binder_name(const std::string& items_path) {
    if (items_path.empty()) {
        return "Item";
    }
    return binder_struct_name(items_path);
}

std::string ui_markup_message(const std::string& relative, ui::UiError err) {
    std::string msg = "invalid UI markup in " + relative;
    switch (err) {
        case ui::UiError::UnknownElement:
            msg += " (unknown element)";
            break;
        case ui::UiError::MissingBinding:
            msg += " (empty or invalid binding)";
            break;
        case ui::UiError::ForbiddenContent:
            msg += " (forbidden content)";
            break;
        case ui::UiError::InvalidMarkup:
            msg += " (invalid XML)";
            break;
    }
    return msg;
}

std::expected<void, CodegenError> note_intern_paths(const ui::BindBinder& binder,
        std::unordered_map<std::uint32_t, std::string>& interned) {
    for (const ui::BindMember& member : binder.members) {
        const ui::BindingId id = ui::intern(member.path);
        if (const auto it = interned.find(id.value); it != interned.end() && it->second != member.path) {
            return std::unexpected(CodegenError{CodegenErrorKind::Collision,
                    "binding intern collision: \"" + it->second + "\" and \"" + member.path + "\""});
        }
        interned.emplace(id.value, member.path);
    }
    for (const auto& [_, nested] : binder.nested) {
        if (auto nested_ok = note_intern_paths(nested, interned); !nested_ok) {
            return nested_ok;
        }
    }
    return {};
}

void emit_binder(std::ostringstream& os, const std::string& name, const ui::BindBinder& binder, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner(static_cast<std::size_t>(indent + 4), ' ');
    const std::string body(static_cast<std::size_t>(indent + 8), ' ');

    os << pad << "struct " << name << " {\n";
    for (const ui::BindMember& member : binder.members) {
        const std::string ident = to_identifier(member.path);
        os << inner << "static constexpr engine::ui::BindingId " << ident << " = engine::ui::intern(\""
           << member.path << "\");\n";
    }
    if (!binder.members.empty()) {
        os << '\n';
    }
    os << inner << "template<typename T>\n";
    if (binder.members.empty()) {
        os << inner << "static void bind(T&) {\n";
    } else {
        os << inner << "static void bind(T& vm) {\n";
    }
    for (const ui::BindMember& member : binder.members) {
        const std::string ident = to_identifier(member.path);
        if (member.is_command) {
            os << body << "vm.command(" << ident << ", vm." << ident << ");\n";
        } else {
            os << body << "vm.property(" << ident << ", vm." << ident << ");\n";
        }
    }
    os << inner << "}\n";

    for (const auto& [items_path, nested] : binder.nested) {
        os << '\n';
        emit_binder(os, nested_binder_name(items_path), nested, indent + 4);
    }
    os << pad << "};\n";
}

std::expected<void, CodegenError> emit_ids_header(const std::vector<EmittedId>& ids, std::string& out) {
    bool any_binder = false;
    for (const EmittedId& id : ids) {
        if (id.ui_binder) {
            any_binder = true;
            break;
        }
    }

    std::ostringstream os;
    os << "#pragma once\n\n";
    os << "#include <engine/resources/asset_id.h>\n";
    if (any_binder) {
        os << "#include <engine/ui/binding_id.h>\n";
    }
    os << '\n';

    for (const EmittedId& id : ids) {
        for (const std::string& ns : id.cpp_id.namespaces) {
            os << "namespace " << ns << " {\n";
        }
        os << "inline constexpr engine::AssetId " << id.cpp_id.name << "{\"" << id.guid.hex() << "\"};\n";
        if (id.ui_binder) {
            os << '\n';
            emit_binder(os, binder_struct_name(id.cpp_id.name), *id.ui_binder, 0);
        }
        for (std::size_t i = 0; i < id.cpp_id.namespaces.size(); ++i) {
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
    std::vector<EmittedId> ids;
    std::unordered_map<std::uint32_t, std::string> interned_paths;

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

        std::optional<ui::BindBinder> ui_binder;
        if (parsed->importer == ImporterKind::Ui) {
            std::ifstream xml_in(file);
            if (!xml_in) {
                return std::unexpected(CodegenError{CodegenErrorKind::Io, "failed to read " + file.generic_string()});
            }
            const std::string xml_text((std::istreambuf_iterator<char>(xml_in)), std::istreambuf_iterator<char>());
            auto binds = ui::scan_bind_tree(xml_text);
            if (!binds) {
                return std::unexpected(CodegenError{CodegenErrorKind::UiMarkup, ui_markup_message(relative, binds.error())});
            }
            if (auto noted = note_intern_paths(*binds, interned_paths); !noted) {
                return std::unexpected(std::move(noted.error()));
            }
            ui_binder = std::move(*binds);
        }

        CatalogEntry catalog_entry;
        catalog_entry.guid = parsed->guid;
        catalog_entry.relative_path = relative;
        catalog_entry.importer = parsed->importer;
        catalog_entry.audio = parsed->audio;
        catalog_entry.texture = parsed->texture;
        output.catalog.add(std::move(catalog_entry));
        ids.push_back(EmittedId{identifier_from_path(relative), parsed->guid, std::move(ui_binder)});
    }

    std::sort(ids.begin(), ids.end(),
            [](const EmittedId& a, const EmittedId& b) { return a.cpp_id.qualified() < b.cpp_id.qualified(); });
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
