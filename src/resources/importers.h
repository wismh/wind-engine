#pragma once

#include <engine/render/graphic_factory.h>

#include <optional>
#include <string_view>

namespace engine {

[[nodiscard]] std::optional<render::MeshDesc> parse_mesh(std::string_view text);
[[nodiscard]] std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml);
[[nodiscard]] std::optional<render::TextureDesc> decode_png_rgba(std::string_view bytes);

}
