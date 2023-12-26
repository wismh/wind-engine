#pragma once

#include <engine/ui/text_line.h>

#include <functional>
#include <string_view>
#include <vector>

namespace engine::ui {

// Greedy word wrap of `text` to `max_width` — the same rules as nvgTextBreakLines: a newline always ends a row (a
// blank line stays a row), spaces/tabs at the start of a row are dropped, a row ends at the last word that still
// fits, and a word wider than `max_width` is split at the nearest UTF-8 character (never fewer than one per row).
// `width_of` measures a slice of `text`. An empty text has no rows.
[[nodiscard]] std::vector<TextLine> break_text_lines(
        std::string_view text, float max_width, const std::function<float(std::string_view)>& width_of);

}
