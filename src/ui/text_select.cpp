#include "ui/text_select.h"

#include "ui/inline_math.h"

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

bool label_text_selectable(const Element& element) noexcept {
    if (element.kind != ElementKind::Label || element.disabled || element.user_select == UserSelect::None) {
        return false;
    }
    if (is_bound(element.command_binding) || is_bound(element.drag_binding)) {
        return false;
    }
    if (element.user_select == UserSelect::Text && text_has_inline_markup(element.text)) {
        return false;
    }
    return true;
}

}
