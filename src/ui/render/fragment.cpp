#include "ui/render/fragment.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::ui {
namespace {

[[nodiscard]] float saturate(float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

constexpr float kPi = std::numbers::pi_v<float>;

}

UiFragment fragment_of(const UiVertex& vertex) noexcept {
    return UiFragment{vertex.uv, vertex.local, vertex.shape, vertex.clip, vertex.aux, unpack_color(vertex.color),
            vertex.kind};
}

UiFragment mix_fragments(const UiFragment& a, const UiFragment& b, float t) noexcept {
    return UiFragment{glm::mix(a.uv, b.uv, t), glm::mix(a.local, b.local, t), glm::mix(a.shape, b.shape, t),
            glm::mix(a.clip, b.clip, t), glm::mix(a.aux, b.aux, t), glm::mix(a.color, b.color, t), a.kind};
}

float box_distance(glm::vec2 p, glm::vec2 half_size, glm::vec2 radii) noexcept {
    const glm::vec2 ap = glm::abs(p);
    const float sharp = std::max(ap.x - half_size.x, ap.y - half_size.y);
    if (radii.x <= 0.0f || radii.y <= 0.0f) {
        return sharp;
    }
    const glm::vec2 q = ap - (half_size - radii);
    if (q.x > 0.0f && q.y > 0.0f) {
        const float k0 = glm::length(q / radii);
        const float k1 = glm::length(q / (radii * radii));
        return k0 * (k0 - 1.0f) / k1;
    }
    return sharp;
}

float clip_coverage(const UiFragment& fragment) noexcept {
    const glm::vec4& c = fragment.clip;
    return saturate(0.5f - (std::abs(c.x) - c.z)) * saturate(0.5f - (std::abs(c.y) - c.w));
}

std::optional<UiTexelFetch> fragment_fetch(const UiFragment& fragment) noexcept {
    switch (fragment.kind) {
        case UiShapeKind::Glyph:
            return UiTexelFetch{UiTextureSlot::Glyph, fragment.uv};
        case UiShapeKind::Mask:
            return UiTexelFetch{UiTextureSlot::Mask, fragment.uv};
        case UiShapeKind::Image:
        case UiShapeKind::ImageRepeat: {
            const glm::vec2 t = fragment.kind == UiShapeKind::Image ? glm::clamp(fragment.uv, 0.0f, 1.0f)
                                                                    : glm::fract(fragment.uv);
            return UiTexelFetch{UiTextureSlot::Image,
                    glm::vec2(fragment.shape.z, fragment.shape.w) + t * fragment.aux};
        }
        case UiShapeKind::LinearGradient:
        case UiShapeKind::RadialGradient:
        case UiShapeKind::ConicGradient: {
            float t = 0.0f;
            if (fragment.kind == UiShapeKind::LinearGradient) {
                t = fragment.uv.x;
            } else if (fragment.kind == UiShapeKind::RadialGradient) {
                t = glm::length(fragment.uv) + fragment.aux.y;
            } else {
                const float turns = std::atan2(fragment.uv.x, -fragment.uv.y) / (2.0f * kPi);
                t = turns - std::floor(turns);
            }
            constexpr auto width = static_cast<float>(kRampWidth);
            return UiTexelFetch{UiTextureSlot::Ramp, {(saturate(t) * (width - 1.0f) + 0.5f) / width, fragment.aux.x}};
        }
        case UiShapeKind::Fill:
        case UiShapeKind::Stroke:
        case UiShapeKind::Line:
        case UiShapeKind::Arc:
            break;
    }
    return std::nullopt;
}

glm::vec4 shade_fragment(const UiFragment& fragment, glm::vec4 texel) noexcept {
    const glm::vec2 half{fragment.shape.x, fragment.shape.y};
    const glm::vec2 radii{fragment.shape.z, fragment.shape.w};
    glm::vec4 out = fragment.color;
    switch (fragment.kind) {
        case UiShapeKind::Fill:
            out *= saturate(0.5f - box_distance(fragment.local, half, radii));
            break;
        case UiShapeKind::Stroke:
            out *= saturate(fragment.aux.x + 0.5f - std::abs(box_distance(fragment.local, half, radii)));
            break;
        case UiShapeKind::Line:
            out *= saturate(fragment.shape.y + 0.5f - std::abs(fragment.local.y)) *
                   saturate(fragment.shape.x + 0.5f - std::abs(fragment.local.x));
            break;
        case UiShapeKind::Arc: {
            const float r = glm::length(fragment.local);
            float cov = saturate(fragment.shape.y + 0.5f - std::abs(r - fragment.shape.x));
            if (fragment.shape.z < kPi) {
                const float gap = fragment.shape.z - std::atan2(std::abs(fragment.local.x), fragment.local.y);
                if (gap < kPi * 0.5f) {
                    cov *= saturate(0.5f + r * std::sin(gap));
                }
            }
            out *= cov;
            break;
        }
        case UiShapeKind::Glyph:
        case UiShapeKind::Mask:
            out *= texel.r;
            break;
        case UiShapeKind::Image:
        case UiShapeKind::ImageRepeat: {
            const glm::vec4 premultiplied{glm::vec3(texel) * texel.a, texel.a};
            out *= premultiplied * saturate(0.5f - box_distance(fragment.local, half, glm::vec2(0.0f)));
            break;
        }
        case UiShapeKind::LinearGradient:
        case UiShapeKind::RadialGradient:
        case UiShapeKind::ConicGradient:
            out *= texel * saturate(0.5f - box_distance(fragment.local, half, radii));
            break;
    }
    return out * clip_coverage(fragment);
}

}
