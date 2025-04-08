#pragma once

#include <engine/ui/document.h>

#include <cstddef>
#include <optional>
#include <string_view>

namespace engine::ui {

class IUiPainter;

// Byte range [begin, end) of `text`.
struct TextRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// The word, whitespace run, or punctuation run that contains `index` (a UTF-8 boundary; an index
// past the end uses the last codepoint). A word is Latin, Cyrillic, or Greek letters and ASCII
// digits; an apostrophe or hyphen sitting between two of those is part of the word. Anything else
// (CJK, emoji) is its own one-codepoint word so a double-click cannot swallow a sentence.
[[nodiscard]] TextRange word_range(std::string_view text, std::size_t index);

// `word_range`, but an inline formula is one span (`\(...\)` in the source, delimiters included)
// and a neighbouring word stops at that span.
[[nodiscard]] TextRange label_word_range(std::string_view text, std::size_t index);

// When `index` sits on a formula span, the caret step that jumps to the far edge. Empty when the
// step is an ordinary character.
[[nodiscard]] std::optional<std::size_t> formula_step(std::string_view text, std::size_t index, bool backward);

// Selection highlight for the boxes in `Element::painted_text_lines`. A formula box is one rect
// when the whole source span is inside the selection.
void paint_text_selection(IUiPainter& painter, const Element& element);

// A Label the pointer may select: `user-select: text` or `all`. A formula is one source span, not
// a reason to ignore the label. A bound command or drag, or `disabled`, keeps the element a
// control instead.
[[nodiscard]] bool label_text_selectable(const Element& element) noexcept;

}
