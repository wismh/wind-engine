#pragma once

#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/painter.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace engine::ui::math {

// Draws a `MathLayout` through an `IUiPainter`: every glyph outline of the formula goes into one `fill_path`
// (one fill, however many glyphs), every rule into a square `fill_rounded_rect`. Outlines come from the
// font's own cache (`MathFont::outline_cached`), so repainting a formula every frame only re-maps points.
//
// `origin` is where the layout's top-left lands in real pixels; every layout unit is multiplied by `scale`
// (the canvas's design-to-real-pixel scale).
void paint_layout(
        IUiPainter& painter, const MathFont& font, const MathLayout& layout, glm::vec2 origin, float scale,
        glm::vec4 color);

}
