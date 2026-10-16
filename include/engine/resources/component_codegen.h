#pragma once

// docs/tech/build/Asset Codegen.md

#include <engine/resources/meta.h>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

struct ScannedComponent {
    std::string include_path;
    std::vector<std::string> namespaces;
    std::string struct_name;
    std::string toml_key;
    std::vector<std::string> fields;

    [[nodiscard]] std::string qualified_name() const;
    bool operator==(const ScannedComponent&) const = default;
};

[[nodiscard]] std::expected<std::vector<ScannedComponent>, CodegenError> scan_components_in_text(
        std::string_view source_text, std::string_view include_path);

[[nodiscard]] std::expected<std::vector<ScannedComponent>, CodegenError> scan_components_in_dirs(
        std::span<const std::filesystem::path> source_dirs);

[[nodiscard]] std::string emit_component_io_header(std::span<const ScannedComponent> components);

[[nodiscard]] std::expected<void, CodegenError> component_codegen_write(
        std::span<const std::filesystem::path> source_dirs, const std::filesystem::path& output_file);

} // namespace engine
