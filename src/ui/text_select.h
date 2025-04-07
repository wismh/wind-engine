#pragma once

#include <engine/ui/document.h>

#include <cstddef>
#include <string_view>

namespace engine::ui {

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

// A Label the pointer may select: `user-select: text` (plain text only — inline `\(...\)` has no
// per-glyph source map) or `user-select: all` (the whole string, formula source included). A bound
// command or drag, or `disabled`, keeps the element a control instead.
[[nodiscard]] bool label_text_selectable(const Element& element) noexcept;

}
