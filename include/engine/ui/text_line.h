#pragma once

// docs/tech/features/UI Input.md

#include <cstddef>
#include <string>
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

// One painted box of a selectable Label, in the same real pixels the glyphs were drawn with. `x`/`y` is the
// top-left of the glyphs after text-align and align-items, not the content box. A plain label has one box per
// visual row. A label with an inline formula has one box per segment: `atomic` is the whole `\(...\)` source
// span (click snaps to an edge), and `drawn` / `source_of_drawn` are set when the drawn bytes are not a slice
// of `Element::text` (an escaped `\\(`). Click-to-index and the selection highlight both read this.
struct PaintedTextLine {
    std::size_t begin = 0;
    std::size_t end = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool atomic = false;
    std::string drawn;
    std::vector<std::size_t> source_of_drawn;
};

}
