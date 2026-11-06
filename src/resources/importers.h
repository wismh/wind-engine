#pragma once

#include <engine/render/graphic_factory.h>
#include <engine/resources/image_io.h>
#include <engine/resources/meta.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

[[nodiscard]] std::optional<render::MeshDesc> parse_mesh(std::string_view text);
// `include_dir` is the shader file's directory. `#include "name"` inside a stage is replaced with that file.
// The name is a single path component. A shader with no include may pass an empty directory.
[[nodiscard]] std::expected<render::ShaderDesc, std::string> parse_shader(
        std::string_view xml, const std::filesystem::path& include_dir = {});
[[nodiscard]] std::optional<render::ShaderDesc> parse_shader_xml(
        std::string_view xml, const std::filesystem::path& include_dir = {});

// One layer per frame of `image` (RGBA8, top row first), in `frames` order, each frame's rows top first. Filter, wrap,
// and mipmaps are left at their defaults for the caller. An error names why not: no frames, more than
// `render::kMaxTextureArrayLayers`, frames of different sizes, or a frame that is not inside the image.
[[nodiscard]] std::expected<render::TextureArrayDesc, std::string> texture_array_from_frames(
        const render::TextureDesc& image, std::span<const SpriteMeta> frames);

}
