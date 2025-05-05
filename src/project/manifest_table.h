#pragma once

#include <engine/project/manifest_error.h>

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

// One parsed manifest file (wind_project.toml, sdk.toml) and typed access to its top-level keys.
class ManifestTable {
public:
    [[nodiscard]] static std::expected<ManifestTable, ManifestFailure> load(const std::filesystem::path& path);

    // nullopt when the key is absent; WrongType when it is not a string.
    [[nodiscard]] std::expected<std::optional<std::string>, ManifestFailure> optional_string(
            std::string_view key) const;
    [[nodiscard]] std::expected<std::string, ManifestFailure> string(std::string_view key) const;
    [[nodiscard]] std::expected<bool, ManifestFailure> boolean(std::string_view key) const;

private:
    explicit ManifestTable(toml::table table);

    toml::table table_;
};

[[nodiscard]] std::string manifest_path_text(const std::filesystem::path& path);

// `failure` with the file's name in front of its detail, for a key error.
[[nodiscard]] ManifestFailure in_manifest(ManifestFailure failure, const std::filesystem::path& path);

}
