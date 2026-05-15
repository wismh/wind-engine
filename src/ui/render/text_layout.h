#pragma once

// Text measurement, line breaking, and glyph-quad layout on the CPU, number for number what the font rasterizer
// produced, so layout keeps every width it had. See docs/tech/architecture/UI Performance Plan.md, step 5.

#include "ui/painter.h"

#include <engine/ui/document.h>
#include <engine/ui/text_line.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace engine::ui {

class FontFace;
class GlyphAtlas;

// One glyph to draw: a rectangle in the caller's units (before any transform) and its texels on an atlas page.
// `uv_min`/`uv_max` are normalized by that page's size, inset one texel into the glyph's padded cell as fontstash does.
struct GlyphQuad {
    std::uint32_t page = 0;
    glm::vec2 min{};
    glm::vec2 max{};
    glm::vec2 uv_min{};
    glm::vec2 uv_max{};
};

// fontstash's size quantization: tenths of a pixel, truncated (`(short)(size * 10.0f)`). Glyphs are cached and
// rasterized per quantized size, and every metric is computed at it, so 13.37 measures as 13.3.
[[nodiscard]] int quantize_text_size(float size) noexcept;

// Text raster scale for a transform whose average axis scale is `average_scale` (that scale times the device pixel
// ratio): quantized to 0.01, capped at 4. Text under a scale is rasterized at size * this, then mapped back down,
// which is why scaled text stays sharp. 1 for an identity transform.
[[nodiscard]] float text_raster_scale(float average_scale, float pixel_ratio = 1.0f) noexcept;

// Font metrics at `size`: ascent and descent positive, line height the font's (ascent + line gap - descent). A size
// that quantizes to nothing gets IUiPainter's 80/20 split of `size`.
[[nodiscard]] TextFontMetrics text_metrics(const FontFace& face, float size);

// measure_text: x is the pen advance (kerning and whole-pixel advances included, not the ink box), y the font's line
// height (or `size` when that is not positive). Stops at the first NUL byte, as a C string would.
[[nodiscard]] glm::vec2 text_extent(FontFace& face, std::string_view text, float size);

// break_lines: rows, in calls of 32 rows each (a call restarts kerning and the \r\n pairing, so the chunking is part of
// the result). `line_height` is the font's. Kept quirk: a row cut inside an overlong word reports the width including
// the character that overflowed.
[[nodiscard]] TextBlock wrap_text(FontFace& face, std::string_view text, float size, float max_width);

// fill_text: appends the quads of `text` at `position` to `out`, aligned like fill_text's UiAlign (Start = left / top,
// Center = center / middle, End = right / bottom; anything else is Start). `raster_scale` is text_raster_scale of the
// current transform: glyphs are rasterized and snapped to whole pixels at size * raster_scale, and the quads come back
// divided by it. Glyphs without ink (a space) advance the pen but add no quad. Stops at the first NUL byte. Returns
// the pen's end x.
float layout_glyphs(FontFace& face, std::uint32_t face_id, GlyphAtlas& atlas, std::string_view text, float size,
        glm::vec2 position, UiAlign horizontal, UiAlign vertical, float raster_scale, std::vector<GlyphQuad>& out);

}
