#pragma once

#include "ui/math/math_ast.h"
#include "ui/math/math_font.h"

#include <glm/vec2.hpp>

#include <vector>

namespace engine::ui::math {

struct LayoutOptions {
    // Size of one em at the base (text / display) level, in the units of the output (px).
    float font_size = 16.0f;
    // Display style (large operators with limits above/below, roomier fractions) instead of text style.
    bool display = false;
};

// One glyph to draw: `origin` is its baseline-left point and `scale` converts font design units to output
// units (`font_size / units_per_em`, already reduced for script levels). The outline's y axis points up, so a
// painter maps a font point (gx, gy) to `origin + (gx * scale, -gy * scale)`.
struct PlacedGlyph {
    GlyphId glyph = 0;
    glm::vec2 origin{};
    float scale = 0.0f;
};

// A filled rectangle (fraction bar, radical vinculum).
struct PlacedRule {
    glm::vec2 position{};  // top-left
    glm::vec2 size{};
};

// Backend-neutral result of laying a formula out: everything a painter needs, no GPU, no font access. The
// coordinate system is y-down with the origin at the top-left of the formula's box, so the baseline sits at
// `y = ascent` and every item lies within [0, width] x [0, ascent + descent].
struct MathLayout {
    float width = 0.0f;
    float ascent = 0.0f;   // box top to baseline
    float descent = 0.0f;  // baseline to box bottom
    std::vector<PlacedGlyph> glyphs;
    std::vector<PlacedRule> rules;

    [[nodiscard]] float height() const noexcept {
        return ascent + descent;
    }
};

// Lays out a parsed formula with TeX's rules (Appendix G, driven by the OpenType MATH constants of `font`):
// atom spacing by class, fraction / script / radical / large-operator / `\left\right` geometry, and
// delimiters stretched through size variants or glyph assemblies.
[[nodiscard]] MathLayout layout_formula(const Row& row, const MathFont& font, const LayoutOptions& options = {});

}
