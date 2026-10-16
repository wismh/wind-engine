#include "ui/render/ui_vertex.h"

#include <algorithm>
#include <cmath>

namespace engine::ui {
namespace {

[[nodiscard]] float saturate(float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

}

std::uint32_t pack_premultiplied(glm::vec4 color, float opacity) noexcept {
    const float alpha = saturate(color.a * opacity);
    const auto channel = [](float v) {
        return static_cast<std::uint32_t>(std::lround(static_cast<double>(saturate(v)) * 255.0));
    };
    return channel(color.r * alpha) | (channel(color.g * alpha) << 8) | (channel(color.b * alpha) << 16) |
           (channel(alpha) << 24);
}

glm::vec4 unpack_color(std::uint32_t color) noexcept {
    return glm::vec4(static_cast<float>(color & 0xFFu), static_cast<float>((color >> 8) & 0xFFu),
                   static_cast<float>((color >> 16) & 0xFFu), static_cast<float>(color >> 24)) /
           255.0f;
}

}
