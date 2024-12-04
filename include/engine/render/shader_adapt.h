#pragma once

#include <engine/render/graphic_factory.h>

#include <string>
#include <string_view>

namespace engine::render {

enum class ShaderTarget { Glsl330Core, Glsl300Es };

[[nodiscard]] std::string adapt_glsl(std::string_view src, ShaderTarget target, bool fragment);

[[nodiscard]] ShaderDesc adapt_shader(const ShaderDesc& src, ShaderTarget target);

}
