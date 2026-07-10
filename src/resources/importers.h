#pragma once

#include <engine/render/graphic_factory.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace engine {

[[nodiscard]] std::optional<render::MeshDesc> parse_mesh(std::string_view text);
[[nodiscard]] std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml);
[[nodiscard]] std::optional<render::TextureDesc> decode_png_rgba(std::string_view bytes);
// In-memory PNG of an RGBA8 image, top row first. Empty on encode failure.
[[nodiscard]] std::vector<std::uint8_t> encode_png_rgba(const render::TextureDesc& image);

}
