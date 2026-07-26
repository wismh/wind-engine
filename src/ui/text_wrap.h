#pragma once

#include <engine/ui/text_line.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

// Greedy word wrap of `text` to `max_width` — the same rules as nvgTextBreakLines: a newline always ends a row (a
// blank line stays a row), spaces/tabs at the start of a row are dropped, a row ends at the last word that still
// fits, and a word wider than `max_width` is split at the nearest UTF-8 character (never fewer than one per row).
// `width_of` measures a slice of `text`. An empty text has no rows.
[[nodiscard]] std::vector<TextLine> break_text_lines(
        std::string_view text, float max_width, const std::function<float(std::string_view)>& width_of);

// `break_text_lines`, plus one empty row when `text` is empty and one more when it ends in `\n`, so a
// caret can sit after the final break. `max_width` of infinity breaks only at newlines.
[[nodiscard]] std::vector<TextLine> editor_lines(
        std::string_view text, float max_width, const std::function<float(std::string_view)>& width_of);

// The row that owns `caret`. A caret equal to a row's `end` stays on that row (the byte before `\n`).
[[nodiscard]] std::size_t editor_line_index(const std::vector<TextLine>& lines, std::size_t caret);

// Distance from the row's left to `caret`, or the row width when `caret` is past `end`.
[[nodiscard]] float editor_caret_x(std::string_view text, const TextLine& line, std::size_t caret,
        const std::function<float(std::string_view)>& width_of);

// UTF-8 boundary on `line` nearest to `goal_x` (0 is `line.begin`).
[[nodiscard]] std::size_t editor_caret_at_x(std::string_view text, const TextLine& line, float goal_x,
        const std::function<float(std::string_view)>& width_of);

// One UTF-8 step. A landing spot strictly between one row's `end` and the next row's `begin` (a `\r`
// left between `\n`s) jumps to that edge, so one press crosses the break.
[[nodiscard]] std::size_t step_editor_caret(std::string_view text, const std::vector<TextLine>& lines,
        std::size_t caret, bool backward);

// `\r\n` and a lone `\r` become `\n`.
[[nodiscard]] std::string normalize_line_breaks(std::string_view text);

}
