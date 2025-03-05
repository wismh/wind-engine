#include "ui/text_wrap.h"

#include "ui/painter.h"

#include <engine/resources/asset_id.h>

namespace engine::ui {

namespace {

[[nodiscard]] bool is_blank(char c) noexcept {
    return c == ' ' || c == '\t';
}

[[nodiscard]] bool is_utf8_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Breaks one newline-free paragraph [begin, end) of `text`, appending its rows to `lines`.
void break_paragraph(std::string_view text, std::size_t begin, std::size_t end, float max_width,
        const std::function<float(std::string_view)>& width_of, std::vector<TextLine>& lines) {
    const std::size_t first_row = lines.size();
    std::size_t pos = begin;
    while (true) {
        while (pos < end && is_blank(text[pos])) {
            ++pos;
        }
        if (pos >= end) {
            break;
        }
        const std::size_t row_begin = pos;
        std::size_t fit_end = row_begin;
        float fit_width = 0.0f;
        bool row_done = false;
        while (!row_done) {
            std::size_t word_end = pos;
            while (word_end < end && !is_blank(text[word_end])) {
                ++word_end;
            }
            const float width = width_of(text.substr(row_begin, word_end - row_begin));
            if (width <= max_width) {
                fit_end = word_end;
                fit_width = width;
                pos = word_end;
                while (pos < end && is_blank(text[pos])) {
                    ++pos;
                }
                if (pos >= end) {
                    lines.push_back({row_begin, fit_end, fit_width});
                    return;
                }
            } else if (fit_end > row_begin) {
                lines.push_back({row_begin, fit_end, fit_width});
                pos = fit_end;
                row_done = true;
            } else {
                // The row's first word alone is too wide: keep as many whole characters as fit (at least one).
                std::size_t cut = row_begin;
                float cut_width = 0.0f;
                for (std::size_t next = row_begin; next < word_end;) {
                    ++next;
                    while (next < word_end && is_utf8_continuation(text[next])) {
                        ++next;
                    }
                    const float w = width_of(text.substr(row_begin, next - row_begin));
                    if (cut != row_begin && w > max_width) {
                        break;
                    }
                    cut = next;
                    cut_width = w;
                }
                lines.push_back({row_begin, cut, cut_width});
                pos = cut;
                row_done = true;
            }
        }
    }
    if (lines.size() == first_row) {
        lines.push_back({begin, begin, 0.0f});
    }
}

}

std::vector<TextLine> break_text_lines(
        std::string_view text, float max_width, const std::function<float(std::string_view)>& width_of) {
    std::vector<TextLine> lines;
    std::size_t begin = 0;
    while (!text.empty()) {
        std::size_t end = text.find('\n', begin);
        const bool last = end == std::string_view::npos;
        if (last) {
            end = text.size();
        }
        std::size_t paragraph_end = end;
        if (paragraph_end > begin && text[paragraph_end - 1] == '\r') {
            --paragraph_end;
        }
        // A trailing newline does not start another (empty) row.
        if (!(last && begin == end && begin > 0)) {
            break_paragraph(text, begin, paragraph_end, max_width, width_of, lines);
        }
        if (last) {
            break;
        }
        begin = end + 1;
    }
    return lines;
}

TextBlock IUiPainter::break_lines(std::string_view text, AssetId font, float size, float max_width) {
    TextBlock block;
    block.line_height = measure_text("M", font, size).y;
    block.lines = break_text_lines(
            text, max_width, [&](std::string_view slice) { return measure_text(slice, font, size).x; });
    return block;
}

}
