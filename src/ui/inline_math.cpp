#include "ui/inline_math.h"

#include "ui/text_select.h"
#include "ui/math/math_layout.h"
#include "ui/math/math_paint.h"
#include "ui/math/math_parser.h"

#include <engine/log.h>
#include <engine/ui/document.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

[[nodiscard]] bool is_escaped_delim(std::string_view text, std::size_t i) noexcept {
    return i + 2 < text.size() && text[i] == '\\' && text[i + 1] == '\\' && (text[i + 2] == '(' || text[i + 2] == ')');
}

[[nodiscard]] bool is_open_delim(std::string_view text, std::size_t i) noexcept {
    return i + 1 < text.size() && text[i] == '\\' && text[i + 1] == '(';
}

[[nodiscard]] bool is_close_delim(std::string_view text, std::size_t i) noexcept {
    return i + 1 < text.size() && text[i] == '\\' && text[i + 1] == ')';
}

// Drawn text accumulated while scanning the source. An escape `\\(` / `\\)` is the only place a drawn
// byte is not `source_begin + offset`; until the first one the map stays empty.
struct TextAccum {
    std::string text;
    std::size_t source_begin = 0;
    bool any = false;
    bool escaped = false;
    std::vector<std::size_t> source_of_drawn;

    void ensure_map() {
        if (escaped) {
            return;
        }
        escaped = true;
        source_of_drawn.resize(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            source_of_drawn[i] = source_begin + i;
        }
    }

    void append(char c, std::size_t source) {
        if (!any) {
            source_begin = source;
            any = true;
        }
        if (escaped) {
            source_of_drawn.push_back(source);
        }
        text.push_back(c);
    }
};

void flush_text(TextAccum& buf, std::vector<InlinePiece>& pieces, std::size_t source_end) {
    if (buf.text.empty()) {
        buf = {};
        return;
    }
    InlinePiece piece;
    piece.kind = InlinePiece::Kind::Text;
    piece.text = std::move(buf.text);
    piece.source_begin = buf.source_begin;
    piece.source_end = source_end;
    if (buf.escaped) {
        buf.source_of_drawn.push_back(source_end);
        piece.source_of_drawn = std::move(buf.source_of_drawn);
    }
    pieces.push_back(std::move(piece));
    buf = {};
}

[[nodiscard]] std::size_t source_at(const InlinePiece& piece, std::size_t drawn) noexcept {
    if (!piece.source_of_drawn.empty()) {
        if (drawn >= piece.source_of_drawn.size()) {
            return piece.source_end;
        }
        return piece.source_of_drawn[drawn];
    }
    return piece.source_begin + drawn;
}

[[nodiscard]] bool is_blank(char c) noexcept { return c == ' ' || c == '\t'; }

[[nodiscard]] bool is_utf8_continuation(char c) noexcept { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

[[nodiscard]] std::size_t next_utf8(std::string_view text, std::size_t i) noexcept {
    if (i >= text.size()) {
        return i;
    }
    ++i;
    while (i < text.size() && is_utf8_continuation(text[i])) {
        ++i;
    }
    return i;
}

struct Span {
    enum class Kind { Text, Math, Para };

    Kind kind = Kind::Text;
    std::size_t piece = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// Text pieces split on `\n` (a preceding `\r` is not part of the row). Math pieces stay whole,
// newline and all. `wrap` false drops the newlines instead of breaking, which is nowrap.
[[nodiscard]] std::vector<Span> build_spans(const std::vector<InlinePiece>& pieces, bool wrap) {
    std::vector<Span> spans;
    for (std::size_t p = 0; p < pieces.size(); ++p) {
        const InlinePiece& piece = pieces[p];
        if (piece.kind == InlinePiece::Kind::Math) {
            spans.push_back(Span{Span::Kind::Math, p, 0, 0});
            continue;
        }
        const std::string& text = piece.text;
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\n') {
                continue;
            }
            std::size_t end = i;
            if (end > start && text[end - 1] == '\r') {
                --end;
            }
            if (end > start) {
                spans.push_back(Span{Span::Kind::Text, p, start, end});
            }
            if (wrap) {
                spans.push_back(Span{Span::Kind::Para, 0, 0, 0});
            }
            start = i + 1;
        }
        if (start < text.size()) {
            spans.push_back(Span{Span::Kind::Text, p, start, text.size()});
        }
    }
    return spans;
}

// Same trailing-newline rule as break_text_lines: a final `\n` does not add an empty row, but a
// leading one does, and `\n\n` keeps the blank row between.
[[nodiscard]] std::vector<std::vector<Span>> paragraphs_of(const std::vector<Span>& spans) {
    std::vector<std::vector<Span>> paragraphs;
    std::vector<Span> current;
    for (const Span& span : spans) {
        if (span.kind == Span::Kind::Para) {
            paragraphs.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(span);
    }
    if (!current.empty() || paragraphs.empty()) {
        paragraphs.push_back(std::move(current));
    }
    return paragraphs;
}

struct Gap {
    bool on = false;
    std::size_t piece = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Tok {
    enum class Kind { Word, Math };

    Kind kind = Kind::Word;
    std::size_t piece = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    bool has_gap = false;
    std::size_t gap_piece = 0;
    std::size_t gap_begin = 0;
    std::size_t gap_end = 0;
};

void take_gap(Tok& tok, Gap& gap) {
    if (!gap.on || gap.begin >= gap.end) {
        gap = {};
        return;
    }
    tok.has_gap = true;
    tok.gap_piece = gap.piece;
    tok.gap_begin = gap.begin;
    tok.gap_end = gap.end;
    gap = {};
}

[[nodiscard]] std::vector<Tok> tokenize(const std::vector<Span>& paragraph, const std::vector<InlinePiece>& pieces) {
    std::vector<Tok> toks;
    Gap pending;
    for (const Span& span : paragraph) {
        if (span.kind == Span::Kind::Math) {
            Tok tok;
            tok.kind = Tok::Kind::Math;
            tok.piece = span.piece;
            take_gap(tok, pending);
            toks.push_back(tok);
            continue;
        }
        const std::string& text = pieces[span.piece].text;
        std::size_t i = span.begin;
        while (i < span.end) {
            const std::size_t gap_begin = i;
            while (i < span.end && is_blank(text[i])) {
                ++i;
            }
            if (i >= span.end) {
                if (i > gap_begin) {
                    if (pending.on && pending.piece == span.piece && pending.end == gap_begin) {
                        pending.end = i;
                    } else {
                        pending = Gap{true, span.piece, gap_begin, i};
                    }
                }
                break;
            }
            if (i > gap_begin) {
                if (pending.on && pending.piece == span.piece && pending.end == gap_begin) {
                    pending.end = i;
                } else {
                    pending = Gap{true, span.piece, gap_begin, i};
                }
            }
            const std::size_t word_begin = i;
            while (i < span.end && !is_blank(text[i])) {
                ++i;
            }
            Tok tok;
            tok.kind = Tok::Kind::Word;
            tok.piece = span.piece;
            tok.begin = word_begin;
            tok.end = i;
            take_gap(tok, pending);
            toks.push_back(tok);
        }
    }
    return toks;
}

[[nodiscard]] bool is_math_piece(const std::vector<InlinePiece>& pieces, std::size_t piece) noexcept {
    return piece < pieces.size() && pieces[piece].kind == InlinePiece::Kind::Math;
}

[[nodiscard]] InlineLine make_line(std::vector<InlineSegment> segments, float width,
                                   const std::vector<InlinePiece>& pieces, TextFontMetrics font, float strut,
                                   const std::function<InlineBox(std::size_t)>& math_box) {
    float ascent = font.ascent;
    float descent = font.descent;
    for (const InlineSegment& seg : segments) {
        if (!is_math_piece(pieces, seg.piece)) {
            continue;
        }
        const InlineBox box = math_box(seg.piece);
        ascent = std::max(ascent, box.ascent);
        descent = std::max(descent, box.descent);
    }
    InlineLine line;
    line.segments = std::move(segments);
    line.width = width;
    line.baseline = ascent;
    line.height = std::max(strut, ascent + descent);
    return line;
}

[[nodiscard]] float line_height_key(const Element& element, float font_size) {
    if (element.line_height.kind == LineHeightKind::Normal) {
        return -1.0f;
    }
    return resolve_line_height(element.line_height, font_size, 0.0f);
}

[[nodiscard]] TextFontMetrics fallback_font_metrics(float font_size) {
    return TextFontMetrics{font_size * 0.8f, font_size * 0.2f, font_size};
}

[[nodiscard]] InlineBox fallback_math_box(std::string_view source, float font_size) {
    if (source.empty()) {
        return {};
    }
    return InlineBox{static_cast<float>(source.size()) * font_size * 0.5f, font_size * 0.8f, font_size * 0.2f};
}

struct LabelInlineCache {
    std::string text;
    AssetId font_family{};
    float font_size = 0.0f;
    float wrap_width = 0.0f;
    float line_height_key = 0.0f;
    bool nowrap = false;
    const math::MathFont* font = nullptr;
    InlineSplit split;
    std::vector<math::MathLayout> formulas;
    InlineLayout layout;
    TextFontMetrics metrics;
};

[[nodiscard]] bool cache_keys_match(const LabelInlineCache& cache, const Element& element, const IUiPainter& painter,
                                    float font_size) {
    return cache.text == element.text && cache.font_family == element.font_family && cache.font_size == font_size &&
           cache.line_height_key == line_height_key(element, font_size) &&
           cache.nowrap == (element.white_space != WhiteSpace::Normal) && cache.font == painter.math_font();
}

// A non-empty text run measured as zero wide. That happens when the painter's transform scale is 0
// (or the font is not in the atlas yet): the formula is then placed at the start of the line, and
// keeping the cache paints it on top of the letters after the scale returns to 1.
[[nodiscard]] bool collapsed_text(const LabelInlineCache& cache) {
    for (const InlineLine& line : cache.layout.lines) {
        for (const InlineSegment& seg : line.segments) {
            if (seg.piece >= cache.split.pieces.size() ||
                cache.split.pieces[seg.piece].kind == InlinePiece::Kind::Math) {
                continue;
            }
            if (seg.end > seg.begin && seg.width <= 0.0f) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] LabelInlineCache build_label_inline(const Element& element, IUiPainter* painter, float font_size,
                                                  float wrap_width, bool warn) {
    LabelInlineCache cache;
    cache.text = element.text;
    cache.font_family = element.font_family;
    cache.font_size = font_size;
    cache.nowrap = element.white_space != WhiteSpace::Normal;
    cache.wrap_width = cache.nowrap ? 0.0f : wrap_width;
    cache.line_height_key = line_height_key(element, font_size);
    cache.font = painter != nullptr ? painter->math_font() : nullptr;
    cache.metrics = painter != nullptr ? painter->font_metrics(element.font_family, font_size)
                                       : fallback_font_metrics(font_size);
    cache.split = split_inline(element.text);
    if (warn && cache.split.unclosed) {
        engine::log::warn("label text '" + element.text + "' has an unclosed \\(; the rest stays plain text");
    }

    float strut = resolve_line_height(element.line_height, font_size, cache.metrics.line_height);
    if (strut <= 0.0f) {
        strut = cache.metrics.line_height > 0.0f ? cache.metrics.line_height : font_size;
    }

    cache.formulas.resize(cache.split.pieces.size());
    for (std::size_t i = 0; i < cache.split.pieces.size(); ++i) {
        const InlinePiece& piece = cache.split.pieces[i];
        if (piece.kind != InlinePiece::Kind::Math || cache.font == nullptr) {
            continue;
        }
        const math::ParseResult parsed = math::parse_formula(piece.text);
        if (warn && !parsed.ok()) {
            engine::log::warn("math formula '" + piece.text + "' has " + std::to_string(parsed.errors.size()) +
                              " error(s), first: " + math::describe(parsed.errors.front()));
        }
        cache.formulas[i] = math::layout_formula(parsed.root, *cache.font, math::LayoutOptions{font_size, false});
    }

    const auto measure = [&](std::size_t piece, std::size_t begin, std::size_t end) {
        if (piece >= cache.split.pieces.size() || begin >= end) {
            return 0.0f;
        }
        const std::string& text = cache.split.pieces[piece].text;
        const std::string_view slice(text.data() + begin, end - begin);
        if (painter != nullptr) {
            return painter->measure_text(slice, element.font_family, font_size).x;
        }
        return static_cast<float>(slice.size()) * font_size * 0.5f;
    };
    const auto math_box = [&](std::size_t piece) {
        if (piece >= cache.split.pieces.size()) {
            return InlineBox{};
        }
        if (cache.font != nullptr) {
            const math::MathLayout& layout = cache.formulas[piece];
            return InlineBox{layout.width, layout.ascent, layout.descent};
        }
        return fallback_math_box(cache.split.pieces[piece].text, font_size);
    };
    cache.layout = layout_inline(cache.split.pieces, cache.nowrap ? 0.0f : wrap_width, !cache.nowrap, strut,
                                 cache.metrics, measure, math_box);
    return cache;
}

void store_cache(const Element& element, LabelInlineCache built) {
    element.inline_cache = std::make_shared<LabelInlineCache>(std::move(built));
}

[[nodiscard]] const LabelInlineCache* stored_cache(const Element& element) {
    return static_cast<const LabelInlineCache*>(element.inline_cache.get());
}

} // namespace

InlineSplit split_inline(std::string_view text) {
    InlineSplit out;
    TextAccum buf;
    buf.text.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (is_escaped_delim(text, i)) {
            buf.ensure_map();
            buf.append('\\', i);
            buf.append(text[i + 2], i + 2);
            i += 3;
            continue;
        }
        if (is_open_delim(text, i)) {
            std::string formula;
            std::size_t j = i + 2;
            bool closed = false;
            while (j < text.size()) {
                if (is_escaped_delim(text, j)) {
                    formula.push_back('\\');
                    formula.push_back(text[j + 2]);
                    j += 3;
                    continue;
                }
                if (is_close_delim(text, j)) {
                    closed = true;
                    break;
                }
                formula.push_back(text[j]);
                ++j;
            }
            if (!closed) {
                out.unclosed = true;
                while (i < text.size()) {
                    buf.append(text[i], i);
                    ++i;
                }
                break;
            }
            flush_text(buf, out.pieces, i);
            InlinePiece piece;
            piece.kind = InlinePiece::Kind::Math;
            piece.text = std::move(formula);
            piece.source_begin = i;
            piece.source_end = j + 2;
            out.pieces.push_back(std::move(piece));
            i = j + 2;
            continue;
        }
        buf.append(text[i], i);
        ++i;
    }
    flush_text(buf, out.pieces, text.size());
    return out;
}

InlineLayout layout_inline(const std::vector<InlinePiece>& pieces, float max_width, bool wrap, float strut,
                           TextFontMetrics font,
                           const std::function<float(std::size_t, std::size_t, std::size_t)>& measure,
                           const std::function<InlineBox(std::size_t)>& math_box) {
    InlineLayout layout;
    if (pieces.empty()) {
        return layout;
    }
    strut = std::max(0.0f, strut);
    if (wrap && !(max_width >= 0.0f)) {
        max_width = 0.0f;
    }

    const auto width_of = [&](std::size_t piece, std::size_t begin, std::size_t end) {
        if (begin >= end) {
            return 0.0f;
        }
        return std::max(0.0f, measure(piece, begin, end));
    };

    std::vector<InlineSegment> segs;
    float line_w = 0.0f;
    const auto finish = [&]() {
        layout.lines.push_back(make_line(std::move(segs), line_w, pieces, font, strut, math_box));
        segs.clear();
        line_w = 0.0f;
    };
    const auto add_text = [&](std::size_t piece, std::size_t begin, std::size_t end, float width) {
        if (begin >= end) {
            return;
        }
        InlineSegment seg;
        seg.piece = piece;
        seg.begin = begin;
        seg.end = end;
        seg.x = line_w;
        seg.width = width;
        segs.push_back(seg);
        line_w += width;
    };
    const auto add_math = [&](std::size_t piece, float width) {
        InlineSegment seg;
        seg.piece = piece;
        seg.x = line_w;
        seg.width = width;
        segs.push_back(seg);
        line_w += width;
    };

    const auto place_word = [&](const Tok& tok) {
        std::size_t begin = tok.begin;
        bool with_gap = tok.has_gap;
        const std::string& text = pieces[tok.piece].text;
        while (begin < tok.end) {
            const bool line_empty = segs.empty();
            if (with_gap) {
                const bool same = tok.gap_piece == tok.piece;
                if (line_empty && wrap) {
                    with_gap = false;
                    continue;
                }
                if (line_empty && !wrap) {
                    add_text(tok.piece, tok.gap_begin, tok.end, width_of(tok.piece, tok.gap_begin, tok.end));
                    return;
                }
                const bool extend = same && !segs.empty() && !is_math_piece(pieces, segs.back().piece) &&
                                    segs.back().piece == tok.piece && segs.back().end == tok.gap_begin;
                if (extend) {
                    const float combined = width_of(tok.piece, segs.back().begin, tok.end);
                    const float delta = combined - segs.back().width;
                    if (!wrap || line_w + delta <= max_width) {
                        segs.back().end = tok.end;
                        segs.back().width = combined;
                        line_w += delta;
                        return;
                    }
                    finish();
                    with_gap = false;
                    continue;
                }
                const float gap_w = width_of(tok.gap_piece, tok.gap_begin, tok.gap_end);
                const float word_w = width_of(tok.piece, begin, tok.end);
                const float need = same ? width_of(tok.piece, tok.gap_begin, tok.end) : gap_w + word_w;
                if (!wrap || line_w + need <= max_width) {
                    if (same) {
                        add_text(tok.piece, tok.gap_begin, tok.end, need);
                    } else {
                        add_text(tok.gap_piece, tok.gap_begin, tok.gap_end, gap_w);
                        add_text(tok.piece, begin, tok.end, word_w);
                    }
                    return;
                }
                finish();
                with_gap = false;
                continue;
            }

            const float word_w = width_of(tok.piece, begin, tok.end);
            if (!line_empty && wrap && line_w + word_w > max_width) {
                finish();
                continue;
            }
            if (line_empty && wrap && word_w > max_width) {
                std::size_t cut = begin;
                float cut_w = 0.0f;
                for (std::size_t next = next_utf8(text, begin); next > cut && next <= tok.end;) {
                    const float cw = width_of(tok.piece, begin, next);
                    if (cut != begin && cw > max_width) {
                        break;
                    }
                    cut = next;
                    cut_w = cw;
                    if (next >= tok.end) {
                        break;
                    }
                    next = next_utf8(text, next);
                }
                if (cut == begin) {
                    cut = std::min(tok.end, next_utf8(text, begin));
                    cut_w = width_of(tok.piece, begin, cut);
                }
                add_text(tok.piece, begin, cut, cut_w);
                if (cut >= tok.end) {
                    return;
                }
                finish();
                begin = cut;
                continue;
            }
            add_text(tok.piece, begin, tok.end, word_w);
            return;
        }
    };

    const auto place_math = [&](const Tok& tok) {
        const InlineBox box = math_box(tok.piece);
        const float gap_w = tok.has_gap ? width_of(tok.gap_piece, tok.gap_begin, tok.gap_end) : 0.0f;
        float added = gap_w + box.width;
        const bool extend = tok.has_gap && !segs.empty() && !is_math_piece(pieces, segs.back().piece) &&
                            segs.back().piece == tok.gap_piece && segs.back().end == tok.gap_begin;
        if (extend) {
            const float combined = width_of(tok.gap_piece, segs.back().begin, tok.gap_end);
            added = (combined - segs.back().width) + box.width;
        }
        if (!segs.empty() && wrap && line_w + added > max_width) {
            finish();
            add_math(tok.piece, box.width);
            return;
        }
        if (segs.empty() && wrap) {
            add_math(tok.piece, box.width);
            return;
        }
        if (extend) {
            const float combined = width_of(tok.gap_piece, segs.back().begin, tok.gap_end);
            line_w += combined - segs.back().width;
            segs.back().end = tok.gap_end;
            segs.back().width = combined;
        } else if (tok.has_gap && gap_w > 0.0f) {
            add_text(tok.gap_piece, tok.gap_begin, tok.gap_end, gap_w);
        }
        add_math(tok.piece, box.width);
    };

    const std::vector<Span> spans = build_spans(pieces, wrap);
    for (const std::vector<Span>& paragraph : paragraphs_of(spans)) {
        bool committed = false;
        segs.clear();
        line_w = 0.0f;
        if (!wrap) {
            for (const Span& span : paragraph) {
                if (span.kind == Span::Kind::Math) {
                    add_math(span.piece, math_box(span.piece).width);
                } else {
                    add_text(span.piece, span.begin, span.end, width_of(span.piece, span.begin, span.end));
                }
            }
            finish();
            committed = true;
            continue;
        }
        for (const Tok& tok : tokenize(paragraph, pieces)) {
            const std::size_t before = layout.lines.size();
            if (tok.kind == Tok::Kind::Math) {
                place_math(tok);
            } else {
                place_word(tok);
            }
            if (layout.lines.size() != before) {
                committed = true;
            }
        }
        if (!segs.empty()) {
            finish();
        } else if (!committed) {
            finish();
        }
    }

    for (const InlineLine& line : layout.lines) {
        layout.width = std::max(layout.width, line.width);
        layout.height += line.height;
    }
    return layout;
}

glm::vec2 measure_label_inline(const Element& element, IUiPainter* painter, float font_size, float wrap_width) {
    if (painter != nullptr) {
        if (const LabelInlineCache* cache = stored_cache(element);
            cache != nullptr && !collapsed_text(*cache) && cache_keys_match(*cache, element, *painter, font_size) &&
            (cache->nowrap || cache->wrap_width == wrap_width)) {
            return {cache->layout.width, cache->layout.height};
        }
        LabelInlineCache built = build_label_inline(element, painter, font_size, wrap_width, true);
        const glm::vec2 size{built.layout.width, built.layout.height};
        store_cache(element, std::move(built));
        return size;
    }
    const LabelInlineCache built = build_label_inline(element, nullptr, font_size, wrap_width, false);
    return {built.layout.width, built.layout.height};
}

void paint_label_inline(IUiPainter& painter, Element& element, float font_size, float content_width,
                        const render::Rect& content, float ui_scale, UiAlign horizontal, UiAlign vertical,
                        glm::vec4 color) {
    constexpr float kEpsilon = 0.01f;
    const LabelInlineCache* cache = stored_cache(element);
    const bool reusable = cache != nullptr && !collapsed_text(*cache) &&
                          cache_keys_match(*cache, element, painter, font_size) &&
                          (cache->nowrap || (cache->wrap_width + kEpsilon >= content_width &&
                                             cache->layout.width <= content_width + kEpsilon));
    if (!reusable) {
        store_cache(element, build_label_inline(element, &painter, font_size, content_width, true));
        cache = stored_cache(element);
    }
    if (cache == nullptr || cache->layout.lines.empty()) {
        if (element.kind == ElementKind::Label) {
            element.painted_text_lines.clear();
        }
        return;
    }

    painter.set_font(element.font_family, font_size * ui_scale);
    const float block_h = cache->layout.height * ui_scale;
    float top = content.y;
    if (vertical == UiAlign::Center) {
        top = content.y + (content.h - block_h) * 0.5f;
    } else if (vertical == UiAlign::End) {
        top = content.y + content.h - block_h;
    }
    top = std::max(top, content.y);

    struct PlacedSeg {
        std::size_t piece = 0;
        std::size_t begin = 0;
        std::size_t end = 0;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float baseline = 0.0f;
        bool math = false;
    };
    std::vector<PlacedSeg> placed;
    const math::MathFont* math_font = painter.math_font();
    float y = top;
    for (const InlineLine& line : cache->layout.lines) {
        const float line_w = line.width * ui_scale;
        float line_left = content.x;
        if (horizontal == UiAlign::Center) {
            line_left = content.x + (content.w - line_w) * 0.5f;
        } else if (horizontal == UiAlign::End) {
            line_left = content.x + content.w - line_w;
        }
        const float baseline = y + line.baseline * ui_scale;
        const float row_h = line.height * ui_scale;
        for (const InlineSegment& seg : line.segments) {
            if (seg.piece >= cache->split.pieces.size()) {
                continue;
            }
            const InlinePiece& piece = cache->split.pieces[seg.piece];
            if (piece.kind != InlinePiece::Kind::Math && (seg.begin >= seg.end || seg.end > piece.text.size())) {
                continue;
            }
            PlacedSeg item;
            item.piece = seg.piece;
            item.begin = seg.begin;
            item.end = seg.end;
            item.x = line_left + seg.x * ui_scale;
            item.y = y;
            item.width = seg.width * ui_scale;
            item.height = row_h;
            item.baseline = baseline;
            item.math = piece.kind == InlinePiece::Kind::Math;
            placed.push_back(item);
        }
        y += row_h;
    }

    if (element.kind == ElementKind::Label) {
        if (element.user_select == UserSelect::None) {
            element.painted_text_lines.clear();
        } else {
            element.painted_font_size_px = font_size * ui_scale;
            element.painted_text_lines.clear();
            element.painted_text_lines.reserve(placed.size());
            for (const PlacedSeg& item : placed) {
                const InlinePiece& piece = cache->split.pieces[item.piece];
                PaintedTextLine box;
                box.x = item.x;
                box.y = item.y;
                box.width = item.width;
                box.height = item.height;
                if (item.math) {
                    box.begin = piece.source_begin;
                    box.end = piece.source_end;
                    box.atomic = true;
                } else {
                    box.begin = source_at(piece, item.begin);
                    box.end = source_at(piece, item.end);
                    if (!piece.source_of_drawn.empty() && item.end < piece.source_of_drawn.size()) {
                        box.drawn = piece.text.substr(item.begin, item.end - item.begin);
                        box.source_of_drawn.assign(
                                piece.source_of_drawn.begin() + static_cast<std::ptrdiff_t>(item.begin),
                                piece.source_of_drawn.begin() + static_cast<std::ptrdiff_t>(item.end + 1));
                    }
                }
                element.painted_text_lines.push_back(std::move(box));
            }
            // Highlight before the glyphs, from the boxes just cached.
            paint_text_selection(painter, element);
        }
    }

    for (const PlacedSeg& item : placed) {
        const InlinePiece& piece = cache->split.pieces[item.piece];
        if (item.math) {
            if (math_font != nullptr && item.piece < cache->formulas.size()) {
                const math::MathLayout& formula = cache->formulas[item.piece];
                math::paint_layout(painter, *math_font, formula,
                        glm::vec2{item.x, item.baseline - formula.ascent * ui_scale}, ui_scale, color);
            }
            continue;
        }
        painter.fill_text(std::string_view(piece.text).substr(item.begin, item.end - item.begin),
                glm::vec2{item.x, item.baseline - cache->metrics.ascent * ui_scale}, color, UiAlign::Start,
                UiAlign::Start);
    }
}

} // namespace engine::ui
