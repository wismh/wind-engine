#include "project/manifest_table.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace engine {

std::string manifest_path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

ManifestFailure in_manifest(ManifestFailure failure, const std::filesystem::path& path) {
    failure.detail = manifest_path_text(path.filename()) + ": " + failure.detail;
    return failure;
}

ManifestTable::ManifestTable(toml::table table)
    : table_(std::move(table)) {}

std::expected<ManifestTable, ManifestFailure> ManifestTable::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(ManifestFailure{ManifestError::Missing, manifest_path_text(path)});
    }
    std::ostringstream text;
    text << in.rdbuf();
    try {
        return ManifestTable{toml::parse(text.str(), manifest_path_text(path))};
    } catch (const toml::parse_error& error) {
        std::ostringstream detail;
        detail << manifest_path_text(path) << ':' << error.source().begin.line << ": " << error.description();
        return std::unexpected(ManifestFailure{ManifestError::Malformed, detail.str()});
    }
}

std::expected<std::optional<std::string>, ManifestFailure> ManifestTable::optional_string(
        std::string_view key) const {
    const toml::node* const node = table_.get(key);
    if (node == nullptr) {
        return std::optional<std::string>{};
    }
    const std::optional<std::string> value = node->value<std::string>();
    if (!node->is_string() || !value) {
        return std::unexpected(ManifestFailure{ManifestError::WrongType, std::string(key) + " must be a string"});
    }
    return value;
}

std::expected<std::string, ManifestFailure> ManifestTable::string(std::string_view key) const {
    std::expected<std::optional<std::string>, ManifestFailure> value = optional_string(key);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    if (!value->has_value()) {
        return std::unexpected(ManifestFailure{ManifestError::MissingKey, std::string(key)});
    }
    return std::move(**value);
}

std::expected<bool, ManifestFailure> ManifestTable::boolean(std::string_view key) const {
    const toml::node* const node = table_.get(key);
    if (node == nullptr) {
        return std::unexpected(ManifestFailure{ManifestError::MissingKey, std::string(key)});
    }
    if (!node->is_boolean()) {
        return std::unexpected(ManifestFailure{ManifestError::WrongType, std::string(key) + " must be true or false"});
    }
    return *node->value<bool>();
}

std::expected<std::vector<std::string>, ManifestFailure> ManifestTable::optional_string_array(
        std::string_view subtable, std::string_view key) const {
    const toml::table* tab = &table_;
    if (!subtable.empty()) {
        const toml::node* const sub_node = table_.get(subtable);
        if (sub_node == nullptr) {
            return std::vector<std::string>{};
        }
        tab = sub_node->as_table();
        if (tab == nullptr) {
            return std::unexpected(
                    ManifestFailure{ManifestError::WrongType, std::string(subtable) + " must be a table"});
        }
    }
    const toml::node* const node = tab->get(key);
    if (node == nullptr) {
        return std::vector<std::string>{};
    }
    const toml::array* const arr = node->as_array();
    if (arr == nullptr) {
        return std::unexpected(
                ManifestFailure{ManifestError::WrongType, std::string(key) + " must be an array of strings"});
    }
    std::vector<std::string> result;
    result.reserve(arr->size());
    for (const toml::node& elem : *arr) {
        const auto val = elem.value<std::string>();
        if (!elem.is_string() || !val) {
            return std::unexpected(
                    ManifestFailure{ManifestError::WrongType, std::string(key) + " elements must be strings"});
        }
        result.push_back(*val);
    }
    return result;
}

}
