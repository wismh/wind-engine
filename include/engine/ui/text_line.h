#pragma once

#include <cstddef>
#include <vector>

namespace engine::ui {

// One wrapped row of a text: the byte range [begin, end) of the source string (leading and trailing spaces and the
// newline that ended it are not part of it) and its measured width.
struct TextLine {
    std::size_t begin = 0;
    std::size_t end = 0;
    float width = 0.0f;
};

// A text broken into rows at one font and size; `line_height` is the vertical distance between rows.
struct TextBlock {
    std::vector<TextLine> lines;
    float line_height = 0.0f;
};

// One painted row of a selectable Label, in the same real pixels the glyphs were drawn with. `x`/`y` is the
// top-left of the glyphs after text-align and align-items, not the content box. Click-to-index and the
// selection highlight both read this so a click cannot land somewhere the glyphs were not drawn.
struct PaintedTextLine {
    std::size_t begin = 0;
    std::size_t end = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

}
