#include "ui/text_select.h"

#include "ui/inline_math.h"
#include "ui/painter.h"

#include <algorithm>
#include <vector>

namespace engine::ui {
namespace {

struct Codepoint {
    char32_t value = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

[[nodiscard]] std::vector<Codepoint> decode_utf8(std::string_view text) {
    std::vector<Codepoint> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        char32_t cp = lead;
        if ((lead & 0x80) == 0) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0 && i + 1 < text.size()) {
            len = 2;
            cp = (static_cast<char32_t>(lead & 0x1F) << 6) |
                    (static_cast<unsigned char>(text[i + 1]) & 0x3F);
        } else if ((lead & 0xF0) == 0xE0 && i + 2 < text.size()) {
            len = 3;
            cp = (static_cast<char32_t>(lead & 0x0F) << 12) |
                    (static_cast<char32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(text[i + 2]) & 0x3F);
        } else if ((lead & 0xF8) == 0xF0 && i + 3 < text.size()) {
            len = 4;
            cp = (static_cast<char32_t>(lead & 0x07) << 18) |
                    (static_cast<char32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                    (static_cast<char32_t>(static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(text[i + 3]) & 0x3F);
        }
        out.push_back(Codepoint{cp, i, i + len});
        i += len;
    }
    return out;
}

[[nodiscard]] bool is_word_char(char32_t cp) noexcept {
    if ((cp >= U'0' && cp <= U'9') || (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z')) {
        return true;
    }
    if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FF)) {
        return true;
    }
    // Latin Extended, Greek and Coptic, Cyrillic (including the supplement Ukrainian uses).
    if ((cp >= 0x0100 && cp <= 0x024F) || (cp >= 0x0370 && cp <= 0x03FF) || (cp >= 0x0400 && cp <= 0x052F)) {
        return true;
    }
    return false;
}

[[nodiscard]] bool is_space(char32_t cp) noexcept {
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == 0x00A0 || cp == 0x3000;
}

// Apostrophe and hyphen join a word only when they sit between two word characters (м'ясо, well-known).
[[nodiscard]] bool is_mid_word(char32_t cp) noexcept {
    return cp == U'\'' || cp == U'-' || cp == 0x2019 || cp == 0x02BC;
}

[[nodiscard]] bool is_punctuation(char32_t cp) noexcept {
    if (cp <= 0x7F) {
        const char c = static_cast<char>(cp);
        const bool letter = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        return !letter && c > ' ' && c < 0x7F;
    }
    return (cp >= 0x00A1 && cp <= 0x00BF) || (cp >= 0x2000 && cp <= 0x206F);
}

enum class CharKind { Word, Space, Punct, Atomic };

[[nodiscard]] CharKind kind_at(const std::vector<Codepoint>& cps, std::size_t index) noexcept {
    const char32_t cp = cps[index].value;
    if (is_space(cp)) {
        return CharKind::Space;
    }
    if (is_word_char(cp)) {
        return CharKind::Word;
    }
    if (is_mid_word(cp) && index > 0 && index + 1 < cps.size() && is_word_char(cps[index - 1].value) &&
            is_word_char(cps[index + 1].value)) {
        return CharKind::Word;
    }
    if (is_punctuation(cp) || is_mid_word(cp)) {
        return CharKind::Punct;
    }
    return CharKind::Atomic;
}

[[nodiscard]] TextRange expand(const std::vector<Codepoint>& cps, std::size_t index, CharKind kind) {
    std::size_t begin = index;
    std::size_t end = index + 1;
    while (begin > 0 && kind_at(cps, begin - 1) == kind) {
        --begin;
    }
    while (end < cps.size() && kind_at(cps, end) == kind) {
        ++end;
    }
    return TextRange{cps[begin].begin, cps[end - 1].end};
}

}

TextRange word_range(std::string_view text, std::size_t index) {
    const std::vector<Codepoint> cps = decode_utf8(text);
    if (cps.empty()) {
        return {};
    }
    std::size_t at = cps.size() - 1;
    if (index < text.size()) {
        for (std::size_t i = 0; i < cps.size(); ++i) {
            if (index < cps[i].end) {
                at = i;
                break;
            }
        }
    }
    const CharKind kind = kind_at(cps, at);
    if (kind == CharKind::Atomic) {
        return TextRange{cps[at].begin, cps[at].end};
    }
    return expand(cps, at, kind);
}

namespace {

[[nodiscard]] std::size_t next_utf8(std::string_view text, std::size_t pos) noexcept {
    if (pos >= text.size()) {
        return text.size();
    }
    ++pos;
    while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
        ++pos;
    }
    return pos;
}

[[nodiscard]] std::vector<TextRange> formula_spans(std::string_view text) {
    std::vector<TextRange> spans;
    if (!text_has_inline_markup(text)) {
        return spans;
    }
    const InlineSplit split = split_inline(text);
    for (const InlinePiece& piece : split.pieces) {
        if (piece.kind == InlinePiece::Kind::Math && piece.source_begin < piece.source_end) {
            spans.push_back(TextRange{piece.source_begin, piece.source_end});
        }
    }
    return spans;
}

}

TextRange label_word_range(std::string_view text, std::size_t index) {
    const std::vector<TextRange> spans = formula_spans(text);
    for (const TextRange& span : spans) {
        if (index >= span.begin && index < span.end) {
            return span;
        }
    }
    TextRange word = word_range(text, index);
    for (const TextRange& span : spans) {
        if (word.begin >= span.end || word.end <= span.begin) {
            continue;
        }
        if (index < span.begin) {
            word.end = std::min(word.end, span.begin);
        } else if (index >= span.end) {
            word.begin = std::max(word.begin, span.end);
        }
    }
    if (word.begin > word.end) {
        return TextRange{index, index};
    }
    return word;
}

std::optional<std::size_t> formula_step(std::string_view text, std::size_t index, bool backward) {
    for (const TextRange& span : formula_spans(text)) {
        if (backward) {
            if (index > span.begin && index <= span.end) {
                return span.begin;
            }
        } else if (index >= span.begin && index < span.end) {
            return span.end;
        }
    }
    return std::nullopt;
}

void paint_text_selection(IUiPainter& painter, const Element& element) {
    if (!element.selection_anchor || *element.selection_anchor == element.caret_position ||
            element.painted_font_size_px <= 0.0f) {
        return;
    }
    const std::size_t sel_start =
            std::min(std::min(*element.selection_anchor, element.caret_position), element.text.size());
    const std::size_t sel_end =
            std::min(std::max(*element.selection_anchor, element.caret_position), element.text.size());
    if (sel_start >= sel_end) {
        return;
    }
    for (const PaintedTextLine& line : element.painted_text_lines) {
        if (line.end > element.text.size() || line.begin > line.end) {
            continue;
        }
        if (line.atomic) {
            if (sel_start <= line.begin && sel_end >= line.end && line.begin < line.end) {
                painter.fill_rounded_rect(render::Rect{line.x, line.y, line.width, line.height}, 0.0f,
                        element.selection_color);
            }
            continue;
        }
        const bool mapped = line.source_of_drawn.size() == line.drawn.size() + 1 && !line.drawn.empty();
        if (!mapped) {
            const std::size_t lo = std::max(sel_start, line.begin);
            const std::size_t hi = std::min(sel_end, line.end);
            if (lo >= hi) {
                continue;
            }
            const std::string_view source = element.text;
            const float start_w = painter.measure_text(source.substr(line.begin, lo - line.begin), element.font_family,
                                                 element.painted_font_size_px)
                                         .x;
            const float end_w = painter.measure_text(source.substr(line.begin, hi - line.begin), element.font_family,
                                               element.painted_font_size_px)
                                       .x;
            painter.fill_rounded_rect(
                    render::Rect{line.x + start_w, line.y, std::max(0.0f, end_w - start_w), line.height}, 0.0f,
                    element.selection_color);
            continue;
        }
        std::size_t drawn_lo = 0;
        std::size_t drawn_hi = 0;
        bool any = false;
        for (std::size_t pos = 0; pos < line.drawn.size();) {
            const std::size_t next = std::min(next_utf8(line.drawn, pos), line.drawn.size());
            if (next <= pos || next >= line.source_of_drawn.size()) {
                break;
            }
            const std::size_t src_begin = line.source_of_drawn[pos];
            const std::size_t src_end = line.source_of_drawn[next];
            if (src_begin < sel_end && src_end > sel_start) {
                if (!any) {
                    drawn_lo = pos;
                    any = true;
                }
                drawn_hi = next;
            } else if (any) {
                break;
            }
            pos = next;
        }
        if (!any || drawn_lo >= drawn_hi) {
            continue;
        }
        const float start_w =
                painter.measure_text(std::string_view(line.drawn).substr(0, drawn_lo), element.font_family,
                             element.painted_font_size_px)
                        .x;
        const float end_w = painter.measure_text(std::string_view(line.drawn).substr(0, drawn_hi), element.font_family,
                                           element.painted_font_size_px)
                                   .x;
        painter.fill_rounded_rect(render::Rect{line.x + start_w, line.y, std::max(0.0f, end_w - start_w), line.height},
                0.0f, element.selection_color);
    }
}

bool label_text_selectable(const Element& element) noexcept {
    if (element.kind != ElementKind::Label || element.disabled || element.user_select == UserSelect::None) {
        return false;
    }
    if (is_bound(element.command_binding) || is_bound(element.drag_binding)) {
        return false;
    }
    return true;
}

}
