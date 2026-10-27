#pragma once

#include <engine/render/graphic_factory.h>
#include <engine/resources/image_io.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

[[nodiscard]] std::optional<render::MeshDesc> parse_mesh(std::string_view text);
[[nodiscard]] std::expected<render::ShaderDesc, std::string> parse_shader(std::string_view xml);
[[nodiscard]] std::optional<render::ShaderDesc> parse_shader_xml(std::string_view xml);

}
