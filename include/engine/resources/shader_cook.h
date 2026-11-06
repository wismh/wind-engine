#pragma once

// docs/tech/build/Asset Codegen.md

#include <engine/core/export.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Host-only. `asset_codegen` registers the Slang cooker; the engine library does not contain it.
// `directory` is the shader file's folder, so `#include "name"` resolves next to it.
using ShaderCookFn = std::function<std::expected<std::vector<std::uint8_t>, std::string>(
        std::string_view xml, const std::filesystem::path& directory)>;

ENGINE_API void set_shader_cooker(ShaderCookFn cooker);

// The registered cooker, or an error when codegen sees a shader and nothing registered one.
[[nodiscard]] ENGINE_API std::expected<std::vector<std::uint8_t>, std::string> cook_shader_asset(
        std::string_view xml, const std::filesystem::path& directory);

}
