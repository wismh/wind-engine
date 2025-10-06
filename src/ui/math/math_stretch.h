#pragma once

#include "ui/math/math_font.h"

#include <vector>

namespace engine::ui::math {

// One glyph of a stretched delimiter / radical, placed by its origin. `y` is the origin's vertical offset
// from the start of the stretched shape (font units, y up); every piece sits at x = 0.
struct StretchPiece {
    GlyphId glyph = 0;
    float y = 0.0f;
};

// The result of stretching a glyph along the vertical axis (font units, y up, relative to the origin of the
// first piece): either one glyph (the base glyph or one of its size variants) or a glyph assembly of
// stacked parts with repeated extenders.
struct StretchedGlyph {
    std::vector<StretchPiece> pieces;
    float width = 0.0f;   // widest piece advance
    float bottom = 0.0f;  // lowest ink of any piece
    float top = 0.0f;     // highest ink of any piece
    bool assembled = false;

    [[nodiscard]] float size() const noexcept {
        return top - bottom;
    }
};

// Picks the smallest size variant of `codepoint` whose advance measurement is at least `min_size` (font
// units); with none big enough, builds the glyph assembly to reach `min_size`; with no assembly either,
// settles for the largest variant. A glyph with no vertical construction comes back as itself.
//
// Assembly follows the OpenType MATH algorithm: every non-extender part once, extenders repeated until the
// stack reaches `min_size` at the minimum connector overlap, then the overlap is widened (within what
// each joint's connectors allow) so the result lands on `min_size` instead of overshooting it.
[[nodiscard]] StretchedGlyph stretch_vertical(const MathFont& font, char32_t codepoint, float min_size);

}
