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

}
