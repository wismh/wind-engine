#include "ui/render/text_layout.h"

#include "ui/render/font_face.h"
#include "ui/render/glyph_atlas.h"
#include "ui/render/utf8_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace engine::ui {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
// The NanoVG painter's break_lines asked nvgTextBreakLines for this many rows per call.
constexpr int kRowsPerCall = 32;

[[nodiscard]] std::string_view up_to_nul(std::string_view text) noexcept {
    return text.substr(0, text.find('\0'));
}

// fontstash's per-size pixel value of a normalized metric: `metric * isize / 10.0f`.
[[nodiscard]] float at_size(float metric, int isize) noexcept {
    return metric * static_cast<float>(isize) / 10.0f;
}

// A placed glyph rectangle in fontstash's scaled pixel space (FONSquad without the texture coordinates).
struct PenQuad {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// FONStextIter + fonsTextIterNext + fons__getQuad, over [begin, text.size()) of `text`, offsets absolute. One step
// decodes one code point; `x` is the pen before it, `next_x` after its kerning and advance. A malformed or truncated
// UTF-8 tail makes a final step that consumes the rest and moves nothing, as fontstash's did.
class TextIter {
public:
    TextIter(FontFace& face, int isize, float start_x, float start_y, std::string_view text, std::size_t begin) :
        x(start_x), y(start_y), next_x(start_x), next_y(start_y), str(begin), next(begin), face_(face), isize_(isize),
        scale_(face.scale(isize)), text_(text) {}

    bool step() {
        str = next;
        glyph = nullptr;
        if (str == text_.size()) {
            return false;
        }
        std::size_t at = str;
        for (; at != text_.size(); ++at) {
            if (decoder_.step(static_cast<unsigned char>(text_[at])) != Utf8Decoder::kAccept) {
                continue;
            }
            ++at;
            x = next_x;
            y = next_y;
            // fons__getGlyph: no glyph at all below 0.2 px; an unmapped code point is glyph 0 (.notdef).
            if (isize_ >= 2) {
                glyph = &face_.glyph_metrics(face_.glyph_index(decoder_.codepoint), isize_);
                place(*glyph);
            }
            prev_glyph = glyph != nullptr ? glyph->glyph : -1;
            break;
        }
        next = at;
        return true;
    }

    // fontstash writes the decoder's partial value into the iterator, so a step that ends mid-sequence still
    // changes the code point nvgTextBreakLines classifies.
    [[nodiscard]] std::uint32_t codepoint() const noexcept { return decoder_.codepoint; }

    float x = 0.0f;
    float y = 0.0f;
    float next_x = 0.0f;
    float next_y = 0.0f;
    std::size_t str = 0;
    std::size_t next = 0;
    int prev_glyph = -1;
    // The glyph this step placed, or nullptr; `quad` is only fresh when it is set.
    const GlyphMetrics* glyph = nullptr;
    PenQuad quad;

private:
    void place(const GlyphMetrics& metrics) {
        if (prev_glyph != -1) {
            const float adv = static_cast<float>(face_.kern_advance(prev_glyph, metrics.glyph)) * scale_;
            // Letter spacing is never set, so fontstash's `adv + spacing + 0.5f` is this; (int) truncates toward 0.
            next_x += static_cast<float>(static_cast<int>(adv + 0.0f + 0.5f));
        }
        // The quad covers the bitmap plus one of the two padding pixels on each side.
        constexpr int kPad = GlyphAtlas::kPadding;
        const float xoff = static_cast<float>(static_cast<std::int16_t>(metrics.box_x0 - kPad + 1));
        const float yoff = static_cast<float>(static_cast<std::int16_t>(metrics.box_y0 - kPad + 1));
        const float w = static_cast<float>(metrics.bitmap_width() + kPad * 2 - 2);
        const float h = static_cast<float>(metrics.bitmap_height() + kPad * 2 - 2);
        const float rx = std::floor(next_x + xoff);
        const float ry = std::floor(next_y + yoff);
        quad = {rx, ry, rx + w, ry + h};
        next_x += static_cast<float>(static_cast<int>(static_cast<float>(metrics.xadv) / 10.0f + 0.5f));
    }

    FontFace& face_;
    int isize_ = 0;
    float scale_ = 0.0f;
    std::string_view text_;
    Utf8Decoder decoder_;
};

// fonsTextBounds' return value from `x`: the pen advance over the whole string.
[[nodiscard]] float pen_advance(FontFace& face, int isize, std::string_view text, float x) {
    TextIter iter(face, isize, x, 0.0f, text, 0);
    while (iter.step()) {
    }
    return iter.next_x - x;
}

// fons__getVertAlign with FONS_ZERO_TOPLEFT: the baseline offset from `y`.
[[nodiscard]] float vertical_offset(const FontFace& face, UiAlign vertical, int isize) {
    switch (vertical) {
        case UiAlign::Center:
            return (face.ascender() + face.descender()) / 2.0f * static_cast<float>(isize) / 10.0f;
        case UiAlign::End:
            return face.descender() * static_cast<float>(isize) / 10.0f;
        case UiAlign::Start:
        default:
            return face.ascender() * static_cast<float>(isize) / 10.0f;
    }
}

enum class CharKind : std::uint8_t {
    Space,
    Newline,
    Char,
    CjkChar,
};

[[nodiscard]] CharKind classify(std::uint32_t codepoint, std::uint32_t previous) noexcept {
    switch (codepoint) {
        case 9:       // \t
        case 11:      // \v
        case 12:      // \f
        case 32:      // space
        case 0x00A0:  // NBSP
            return CharKind::Space;
        case 10:  // \n
            return previous == 13 ? CharKind::Space : CharKind::Newline;
        case 13:  // \r
            return previous == 10 ? CharKind::Space : CharKind::Newline;
        case 0x0085:  // NEL
            return CharKind::Newline;
        default:
            if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) || (codepoint >= 0x3000 && codepoint <= 0x30FF) ||
                    (codepoint >= 0xFF00 && codepoint <= 0xFFEF) || (codepoint >= 0x1100 && codepoint <= 0x11FF) ||
                    (codepoint >= 0x3130 && codepoint <= 0x318F) || (codepoint >= 0xAC00 && codepoint <= 0xD7AF)) {
                return CharKind::CjkChar;
            }
            return CharKind::Char;
    }
}

struct Row {
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t next = 0;
    float width = 0.0f;
};

// nvgTextBreakLines from `begin` at raster scale 1, at most `rows.size()` rows (minx/maxx dropped: nothing reads
// them). The control flow and every float are kept as NanoVG had them.
int break_rows(FontFace& face, int isize, std::string_view text, std::size_t begin, float break_width,
        std::span<Row> rows) {
    if (rows.empty() || begin == text.size()) {
        return 0;
    }
    const int max_rows = static_cast<int>(rows.size());
    int count = 0;
    float row_start_x = 0.0f;
    float row_width = 0.0f;
    std::size_t row_start = kNone;
    std::size_t row_end = kNone;
    std::size_t word_start = kNone;
    float word_start_x = 0.0f;
    std::size_t break_end = kNone;
    float break_row_width = 0.0f;
    CharKind kind = CharKind::Space;
    CharKind previous_kind = CharKind::Space;
    std::uint32_t previous_codepoint = 0;

    TextIter iter(face, isize, 0.0f, 0.0f, text, begin);
    while (iter.step()) {
        kind = classify(iter.codepoint(), previous_codepoint);
        const bool is_char = kind == CharKind::Char || kind == CharKind::CjkChar;
        if (kind == CharKind::Newline) {
            // Always handle new lines.
            rows[count++] = {row_start != kNone ? row_start : iter.str, row_end != kNone ? row_end : iter.str,
                    iter.next, row_width};
            if (count >= max_rows) {
                return count;
            }
            break_end = row_start;
            break_row_width = 0.0f;
            // Skip the white space at the beginning of the next row.
            row_start = kNone;
            row_end = kNone;
            row_width = 0.0f;
        } else if (row_start == kNone) {
            if (is_char) {
                row_start_x = iter.x;
                row_start = iter.str;
                row_end = iter.next;
                row_width = iter.next_x - row_start_x;
                word_start = iter.str;
                word_start_x = iter.x;
                break_end = row_start;
                break_row_width = 0.0f;
            }
        } else {
            const float next_width = iter.next_x - row_start_x;
            const bool previous_is_char = previous_kind == CharKind::Char || previous_kind == CharKind::CjkChar;
            if (is_char) {
                row_end = iter.next;
                row_width = iter.next_x - row_start_x;
            }
            // The last end of a word.
            if ((previous_is_char && kind == CharKind::Space) || kind == CharKind::CjkChar) {
                break_end = iter.str;
                break_row_width = row_width;
            }
            // The last beginning of a word.
            if ((previous_kind == CharKind::Space && is_char) || kind == CharKind::CjkChar) {
                word_start = iter.str;
                word_start_x = iter.x;
            }
            if (is_char && next_width > break_width) {
                if (break_end == row_start) {
                    // The word is longer than the row: break it here.
                    rows[count++] = {row_start, iter.str, iter.str, row_width};
                    if (count >= max_rows) {
                        return count;
                    }
                    row_start_x = iter.x;
                    row_start = iter.str;
                    row_end = iter.next;
                    row_width = iter.next_x - row_start_x;
                    word_start = iter.str;
                    word_start_x = iter.x;
                } else {
                    // Break after the last word; the new row starts at the current one.
                    rows[count++] = {row_start, break_end, word_start, break_row_width};
                    if (count >= max_rows) {
                        return count;
                    }
                    row_start_x = word_start_x;
                    row_start = word_start;
                    row_end = iter.next;
                    row_width = iter.next_x - row_start_x;
                }
                break_end = row_start;
                break_row_width = 0.0f;
            }
        }
        previous_codepoint = iter.codepoint();
        previous_kind = kind;
    }
    if (row_start != kNone) {
        rows[count++] = {row_start, row_end, text.size(), row_width};
    }
    return count;
}

}

int quantize_text_size(float size) noexcept {
    const float tenths = size * 10.0f;
    if (!(tenths > -32768.0f)) {
        return -32768;
    }
    if (tenths > 32767.0f) {
        return 32767;
    }
    return static_cast<std::int16_t>(tenths);
}

float text_raster_scale(float average_scale, float pixel_ratio) noexcept {
    constexpr float kStep = 0.01f;
    const float quantized = static_cast<float>(static_cast<int>(average_scale / kStep + 0.5f)) * kStep;
    return std::min(quantized, 4.0f) * pixel_ratio;
}

TextFontMetrics text_metrics(const FontFace& face, float size) {
    const int isize = quantize_text_size(size);
    const float ascender = at_size(face.ascender(), isize);
    const float descender = at_size(face.descender(), isize);
    const float line_height = at_size(face.line_height(), isize);
    if (line_height <= 0.0f && ascender <= 0.0f) {
        // The NanoVG painter fell back to IUiPainter::font_metrics, whose measure of "M" came back `size` high here.
        return TextFontMetrics{size * 0.8f, size * 0.2f, size};
    }
    TextFontMetrics metrics;
    metrics.ascent = std::max(0.0f, ascender);
    metrics.descent = std::max(0.0f, -descender);
    metrics.line_height = line_height > 0.0f ? line_height : metrics.ascent + metrics.descent;
    return metrics;
}

glm::vec2 text_extent(FontFace& face, std::string_view text, float size) {
    const int isize = quantize_text_size(size);
    const float advance = pen_advance(face, isize, up_to_nul(text), 0.0f);
    // nvgTextBounds' height is fonsLineBounds at ALIGN_TOP: from 0 to the line height.
    const float height = std::max(0.0f, at_size(face.line_height(), isize));
    return {std::max(0.0f, advance), height > 0.0f ? height : size};
}

TextBlock wrap_text(FontFace& face, std::string_view text, float size, float max_width) {
    const int isize = quantize_text_size(size);
    TextBlock block;
    block.line_height = at_size(face.line_height(), isize);
    std::array<Row, kRowsPerCall> rows{};
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const int count = break_rows(face, isize, text, cursor, max_width, rows);
        if (count <= 0) {
            break;
        }
        for (int i = 0; i < count; ++i) {
            block.lines.push_back({rows[static_cast<std::size_t>(i)].start, rows[static_cast<std::size_t>(i)].end,
                    rows[static_cast<std::size_t>(i)].width});
        }
        cursor = rows[static_cast<std::size_t>(count - 1)].next;
    }
    return block;
}

float layout_glyphs(FontFace& face, std::uint32_t face_id, GlyphAtlas& atlas, std::string_view text, float size,
        glm::vec2 position, UiAlign horizontal, UiAlign vertical, float raster_scale, std::vector<GlyphQuad>& out) {
    text = up_to_nul(text);
    const int isize = quantize_text_size(size * raster_scale);
    if (text.empty() || isize < 2 || !(raster_scale > 0.0f)) {
        return position.x;
    }
    const float inv_scale = 1.0f / raster_scale;
    float x = position.x * raster_scale;
    float y = position.y * raster_scale;
    if (horizontal == UiAlign::End) {
        x -= pen_advance(face, isize, text, x);
    } else if (horizontal == UiAlign::Center) {
        x -= pen_advance(face, isize, text, x) * 0.5f;
    }
    y += vertical_offset(face, vertical, isize);

    TextIter iter(face, isize, x, y, text, 0);
    while (iter.step()) {
        if (iter.glyph == nullptr || iter.glyph->bitmap_width() <= 0 || iter.glyph->bitmap_height() <= 0) {
            continue;
        }
        const AtlasGlyph& placed = atlas.glyph(face, face_id, *iter.glyph, isize);
        const AtlasPage& page = atlas.page(placed.page);
        const float itw = 1.0f / static_cast<float>(page.width());
        const float ith = 1.0f / static_cast<float>(page.height());
        GlyphQuad quad;
        quad.page = placed.page;
        quad.min = {iter.quad.x0 * inv_scale, iter.quad.y0 * inv_scale};
        quad.max = {iter.quad.x1 * inv_scale, iter.quad.y1 * inv_scale};
        quad.uv_min = {static_cast<float>(placed.cell.x0 + 1) * itw, static_cast<float>(placed.cell.y0 + 1) * ith};
        quad.uv_max = {static_cast<float>(placed.cell.x1 - 1) * itw, static_cast<float>(placed.cell.y1 - 1) * ith};
        out.push_back(quad);
    }
    return iter.next_x / raster_scale;
}

}
