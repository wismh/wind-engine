#pragma once

#include <engine/render/graphic_factory.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

// `include_dir` is the shader file's directory. `#include "name"` inside a stage is replaced with that file.
// The name is a single path component. A shader with no include may pass an empty directory.
[[nodiscard]] std::expected<render::ShaderDesc, std::string> parse_shader(
        std::string_view xml, const std::filesystem::path& include_dir = {});

[[nodiscard]] std::optional<render::ShaderDesc> parse_shader_xml(
        std::string_view xml, const std::filesystem::path& include_dir = {});

}
