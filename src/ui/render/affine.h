#pragma once

#include <glm/vec2.hpp>

#include <cmath>

namespace engine::ui {

// A 2x3 affine map in NanoVG's layout: `{a, b, c, d, e, f}` maps (x, y) to (a x + c y + e, b x + d y + f). The UI
// painter only composes translations, rotations, and uniform scales, so in practice it is a similarity; the math here
// does not assume it.
struct Affine2 {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float e = 0.0f;
    float f = 0.0f;

    [[nodiscard]] static constexpr Affine2 translation(glm::vec2 t) noexcept {
        return {1.0f, 0.0f, 0.0f, 1.0f, t.x, t.y};
    }
    [[nodiscard]] static constexpr Affine2 scaling(float s) noexcept { return {s, 0.0f, 0.0f, s, 0.0f, 0.0f}; }
    [[nodiscard]] static Affine2 rotation(float radians) noexcept {
        const float cs = std::cos(radians);
        const float sn = std::sin(radians);
        return {cs, sn, -sn, cs, 0.0f, 0.0f};
    }

    [[nodiscard]] constexpr glm::vec2 apply(glm::vec2 p) const noexcept {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
    // The linear part only (a direction or an offset).
    [[nodiscard]] constexpr glm::vec2 apply_vector(glm::vec2 v) const noexcept {
        return {a * v.x + c * v.y, b * v.x + d * v.y};
    }
    [[nodiscard]] constexpr float determinant() const noexcept { return a * d - c * b; }
    // nvg__getAverageScale: the mean length of the two rows of the linear part.
    [[nodiscard]] float average_scale() const noexcept {
        return (std::sqrt(a * a + c * c) + std::sqrt(b * b + d * d)) * 0.5f;
    }

    // NanoVG's nvgTransformInverse: identity when the map is singular.
    [[nodiscard]] Affine2 inverse() const noexcept {
        const double det = static_cast<double>(a) * d - static_cast<double>(c) * b;
        if (det > -1e-6 && det < 1e-6) {
            return {};
        }
        const double inv = 1.0 / det;
        Affine2 out;
        out.a = static_cast<float>(d * inv);
        out.c = static_cast<float>(-c * inv);
        out.e = static_cast<float>((static_cast<double>(c) * f - static_cast<double>(d) * e) * inv);
        out.b = static_cast<float>(-b * inv);
        out.d = static_cast<float>(a * inv);
        out.f = static_cast<float>((static_cast<double>(b) * e - static_cast<double>(a) * f) * inv);
        return out;
    }
};

// `outer` after `inner`: p -> outer(inner(p)). NanoVG's nvgTranslate / nvgRotate / nvgScale set
// `xform = compose(xform, op)`, so the newest operation applies to points first.
[[nodiscard]] constexpr Affine2 compose(const Affine2& outer, const Affine2& inner) noexcept {
    return {
            outer.a * inner.a + outer.c * inner.b,
            outer.b * inner.a + outer.d * inner.b,
            outer.a * inner.c + outer.c * inner.d,
            outer.b * inner.c + outer.d * inner.d,
            outer.a * inner.e + outer.c * inner.f + outer.e,
            outer.b * inner.e + outer.d * inner.f + outer.f,
    };
}

}
