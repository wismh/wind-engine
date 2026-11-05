#pragma once

// docs/tech/build/Pipeline.md

#include <engine/core/export.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Host-only. `asset_codegen` registers the glTF cooker; the engine library does not contain it. `directory` is the
// folder the source file lives in, so external images and buffers resolve next to it.
using ModelCookFn = std::function<std::expected<std::vector<std::uint8_t>, std::string>(
        std::string_view bytes, const std::filesystem::path& directory)>;

ENGINE_API void set_model_cooker(ModelCookFn cooker);

// The registered cooker, or an error when codegen sees a model and nothing registered one.
[[nodiscard]] ENGINE_API std::expected<std::vector<std::uint8_t>, std::string> cook_model_asset(
        std::string_view bytes, const std::filesystem::path& directory);

}
