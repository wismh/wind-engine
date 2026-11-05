#pragma once

#include <engine/resources/meta.h>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::gltf {

struct CookError {
    std::string message;
};

// glTF or GLB bytes into the engine model blob. `base_dir` resolves external buffers and images.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, CookError> cook_gltf(
        std::string_view bytes,
        const std::filesystem::path& base_dir,
        std::span<const ModelMarkerMeta> markers = {});

// Points `cook_model_asset` at `cook_gltf`. Call once from the host tool or from a test.
void register_gltf_cooker();

}
