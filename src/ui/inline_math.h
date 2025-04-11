#pragma once

#include "ui/painter.h"

#include <glm/vec4.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

// True when `text` contains the two-character sequences `\(` or `\)` — a real delimiter or the
// escaped form `\\(` / `\\)`. Label/Button text that matches is laid out as inline runs; anything
// else keeps the plain `fill_text` path.
[[nodiscard]] inline bool text_has_inline_markup(std::string_view text) noexcept {
    return text.find("\\(") != std::string_view::npos || text.find("\\)") != std::string_view::npos;
}

// True when `text` contains `\(` (including inside the escape `\\(`). A Label/Button with that
// asks for the builtin math font; a string that only escapes `\)` does not.
[[nodiscard]] inline bool text_has_inline_math_delimiter(std::string_view text) noexcept {
    return text.find("\\(") != std::string_view::npos;
}

// One slice of a label string after `\(...\)` delimiters are resolved. `text` is the characters to
// draw (a Text run, escapes already turned into a literal `\(` / `\)`) or the TeX source (a Math
// run, delimiters removed). `source_begin` / `source_end` is that slice in the original string:
// a Math run includes the `\(` and `\)` delimiters. `source_of_drawn` is empty when every drawn
// byte is one source byte from `source_begin`; otherwise it has `text.size() + 1` entries, the
// source offset of each drawn byte and then `source_end` (`\\(` is three source bytes and two
// drawn bytes).
struct InlinePiece {
    enum class Kind { Text, Math };

    Kind kind = Kind::Text;
    std::string text;
    std::size_t source_begin = 0;
    std::size_t source_end = 0;
    std::vector<std::size_t> source_of_drawn;
};

struct InlineSplit {
    std::vector<InlinePiece> pieces;
    // An opening `\(` had no `\)`. The tail, including the opener, is one Text piece.
    bool unclosed = false;
};

// Splits `text` on inline-math delimiters. `\\(` and `\\)` are literals `\(` and `\)` and do not
// open or close a formula. A newline inside `\(...\)` stays in the formula source (the TeX parser
// treats it as space). An unclosed `\(` does not swallow the rest into a formula.
[[nodiscard]] InlineSplit split_inline(std::string_view text);

// The box a math piece occupies. `ascent` / `descent` are relative to the formula baseline, y down
// not included: ascent is above the baseline, descent below it.
struct InlineBox {
    float width = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
};

// One item on a line. For a Text piece, [begin, end) indexes that piece's text. For a Math piece,
// begin and end are 0 and `width` is the formula box.
struct InlineSegment {
    std::size_t piece = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    float x = 0.0f;
    float width = 0.0f;
};

// `baseline` is the distance from this line's top down to the shared text/formula baseline.
// `height` is the stride to the next line: at least `strut`, and taller when a formula's
// ascent + descent exceeds it.
struct InlineLine {
    std::vector<InlineSegment> segments;
    float width = 0.0f;
    float baseline = 0.0f;
    float height = 0.0f;
};

struct InlineLayout {
    std::vector<InlineLine> lines;
    float width = 0.0f;
    float height = 0.0f;
};

// Breaks `pieces` into lines. `wrap` false keeps one line and skips `\n` (white-space: nowrap).
// `wrap` true breaks on `\n` and on width: a math piece is one unbreakable box (never split by
// character; a box wider than the line sits alone and may stick out). `measure` is the width of a
// text slice. `math_box` is the formula box for a Math piece. `strut` is the resolved line-height
// and the minimum line stride; `font` supplies the text's own ascent and descent so a short
// formula shares the text baseline inside that strut.
[[nodiscard]] InlineLayout
layout_inline(const std::vector<InlinePiece>& pieces, float max_width, bool wrap, float strut, TextFontMetrics font,
              const std::function<float(std::size_t piece, std::size_t begin, std::size_t end)>& measure,
              const std::function<InlineBox(std::size_t piece)>& math_box);

// Content-box size of a Label/Button whose text has inline markup. Painter-less layout uses the
// rough per-byte width and does not fill `Element::inline_cache`.
[[nodiscard]] glm::vec2 measure_label_inline(const Element& element, IUiPainter* painter, float font_size,
                                             float wrap_width);

// Draws the cached inline runs inside `content` (real pixels). `content_width` is the design-pixel
// width the lines wrap at; a cache built for a wider wrap is reused when its lines still fit.
void paint_label_inline(IUiPainter& painter, Element& element, float font_size, float content_width,
                        const render::Rect& content, float ui_scale, UiAlign horizontal, UiAlign vertical,
                        glm::vec4 color);

} // namespace engine::ui
