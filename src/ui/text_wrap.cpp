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

std::vector<TextLine> editor_lines(
        std::string_view text, float max_width, const std::function<float(std::string_view)>& width_of) {
    std::vector<TextLine> lines = break_text_lines(text, max_width, width_of);
    if (lines.empty()) {
        lines.push_back(TextLine{0, 0, 0.0f});
    }
    if (!text.empty() && text.back() == '\n') {
        lines.push_back(TextLine{text.size(), text.size(), 0.0f});
    }
    return lines;
}

std::size_t editor_line_index(const std::vector<TextLine>& lines, std::size_t caret) {
    if (lines.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool next_owns = i + 1 < lines.size() && lines[i + 1].begin == caret && lines[i].end != caret;
        if (!next_owns && caret >= lines[i].begin && caret <= lines[i].end) {
            return i;
        }
    }
    return lines.size() - 1;
}

float editor_caret_x(std::string_view text, const TextLine& line, std::size_t caret,
        const std::function<float(std::string_view)>& width_of) {
    if (caret <= line.begin || line.begin > text.size()) {
        return 0.0f;
    }
    const std::size_t end = std::min(caret, std::min(line.end, text.size()));
    if (end <= line.begin) {
        return 0.0f;
    }
    return width_of(text.substr(line.begin, end - line.begin));
}

namespace {

std::size_t next_byte(std::string_view text, std::size_t pos) {
    if (pos >= text.size()) {
        return text.size();
    }
    ++pos;
    while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
        ++pos;
    }
    return pos;
}

std::size_t prev_byte(std::string_view text, std::size_t pos) {
    if (pos == 0 || pos > text.size()) {
        return 0;
    }
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

}

std::size_t editor_caret_at_x(std::string_view text, const TextLine& line, float goal_x,
        const std::function<float(std::string_view)>& width_of) {
    if (line.begin >= line.end || line.end > text.size() || goal_x <= 0.0f) {
        return line.begin;
    }
    std::size_t pos = line.begin;
    float measured = 0.0f;
    while (pos < line.end) {
        const std::size_t next = std::min(next_byte(text, pos), line.end);
        if (next <= pos) {
            break;
        }
        const float next_w = width_of(text.substr(line.begin, next - line.begin));
        if (goal_x < (measured + next_w) * 0.5f) {
            return pos;
        }
        pos = next;
        measured = next_w;
    }
    return line.end;
}

std::size_t step_editor_caret(std::string_view text, const std::vector<TextLine>& lines, std::size_t caret,
        bool backward) {
    const std::size_t next = backward ? prev_byte(text, caret) : next_byte(text, caret);
    if (next == caret || lines.size() < 2) {
        return next;
    }
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        if (next > lines[i].end && next < lines[i + 1].begin) {
            return backward ? lines[i].end : lines[i + 1].begin;
        }
    }
    return next;
}

std::string normalize_line_breaks(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

TextFontMetrics IUiPainter::font_metrics(AssetId font, float size) {
    const float measured = measure_text("M", font, size).y;
    const float line = measured > 0.0f ? measured : size;
    return TextFontMetrics{line * 0.8f, line * 0.2f, line};
}

TextBlock IUiPainter::break_lines(std::string_view text, AssetId font, float size, float max_width) {
    TextBlock block;
    block.line_height = measure_text("M", font, size).y;
    block.lines = break_text_lines(
            text, max_width, [&](std::string_view slice) { return measure_text(slice, font, size).x; });
    return block;
}

}
