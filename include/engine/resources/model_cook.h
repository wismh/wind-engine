#pragma once

// docs/tech/build/Pipeline.md

#include <engine/core/export.h>
#include <engine/resources/meta.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Host-only. `asset_codegen` registers the glTF cooker; the engine library does not contain it. `directory` is the
// folder the source file lives in, so external images and buffers resolve next to it.
using ModelCookFn = std::function<std::expected<std::vector<std::uint8_t>, std::string>(
        std::string_view bytes, const std::filesystem::path& directory, std::span<const ModelMarkerMeta> markers)>;

ENGINE_API void set_model_cooker(ModelCookFn cooker);

// The registered cooker, or an error when codegen sees a model and nothing registered one.
// `markers` come from the model's `.meta`. An empty span leaves every clip without markers.
[[nodiscard]] ENGINE_API std::expected<std::vector<std::uint8_t>, std::string> cook_model_asset(
        std::string_view bytes, const std::filesystem::path& directory, std::span<const ModelMarkerMeta> markers = {});

}
