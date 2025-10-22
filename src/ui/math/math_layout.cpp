#include "ui/math/math_layout.h"

#include "ui/math/math_stretch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace engine::ui::math {
namespace {

// TeX's four math styles. Level 0 = display, 1 = text, 2 = script, 3 = scriptscript; `cramped` keeps
// superscripts lower (denominators, radicands, subscripts).
struct Style {
    int level = 1;
    bool cramped = false;
};

Style script_style(Style style, bool cramped) {
    return {style.level >= 2 ? 3 : 2, cramped};
}

Style cramped_style(Style style) {
    return {style.level, true};
}

Style numerator_style(Style style) {
    return {std::min(style.level + 1, 3), style.cramped};
}

Style denominator_style(Style style) {
    return {std::min(style.level + 1, 3), true};
}

constexpr float kNullDelimiterEm = 0.12f;      // TeX \nulldelimiterspace (1.2pt at 10pt)
constexpr float kDelimiterShortfallEm = 0.5f;  // TeX \delimitershortfall (5pt at 10pt)
constexpr float kDelimiterFactor = 1.802f;     // TeX \delimiterfactor 901 applied to the half-height's double
constexpr char32_t kRadicalCodepoint = 0x221A;

// Everything below is measured in the formula's output unit (px), y up, relative to the box's own origin
// (left edge, baseline). The y-down conversion happens once, at the very end.
struct BoxGlyph {
    GlyphId glyph = 0;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 0.0f;
};

struct BoxRule {
    float x = 0.0f;
    float y = 0.0f;  // bottom edge
    float width = 0.0f;
    float height = 0.0f;
};

struct Box {
    float width = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    // MATH italic correction of the glyph this box is. Not part of `width`: in an OpenType math font the
    // advance already covers the glyph's overhang, and the correction says how far a subscript must be pulled
    // back left of the advance (an integral's lower hook) while a superscript stays at the advance.
    float italic = 0.0f;
    std::vector<BoxGlyph> glyphs;
    std::vector<BoxRule> rules;
};

// Puts `src` into `dst` with its origin at (dx, dy) and grows `dst`'s bounds to cover it.
void place(Box& dst, const Box& src, float dx, float dy) {
    for (const BoxGlyph& g : src.glyphs) {
        dst.glyphs.push_back({g.glyph, g.x + dx, g.y + dy, g.scale});
    }
    for (const BoxRule& r : src.rules) {
        dst.rules.push_back({r.x + dx, r.y + dy, r.width, r.height});
    }
    dst.width = std::max(dst.width, dx + src.width);
    dst.ascent = std::max(dst.ascent, src.ascent + dy);
    dst.descent = std::max(dst.descent, src.descent - dy);
}

// Moves a box's content up by `dy` (its baseline moves down relative to the content).
void shift_up(Box& box, float dy) {
    for (BoxGlyph& g : box.glyphs) {
        g.y += dy;
    }
    for (BoxRule& r : box.rules) {
        r.y += dy;
    }
    box.ascent += dy;
    box.descent -= dy;
}

enum class AtomClass { None, Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

constexpr std::size_t index_of(AtomClass c) {
    return static_cast<std::size_t>(c) - 1;
}

AtomClass to_atom_class(MathClass c) {
    switch (c) {
        case MathClass::Ord:
            return AtomClass::Ord;
        case MathClass::Op:
            return AtomClass::Op;
        case MathClass::Bin:
            return AtomClass::Bin;
        case MathClass::Rel:
            return AtomClass::Rel;
        case MathClass::Open:
            return AtomClass::Open;
        case MathClass::Close:
            return AtomClass::Close;
        case MathClass::Punct:
            return AtomClass::Punct;
    }
    return AtomClass::Ord;
}

// TeXbook p.170. 0 none, 1/2/3 thin/medium/thick (3/4/5 mu), 4/5/6 the same but only outside script styles
// (the parenthesised entries), -1 impossible after Bin conversion.
constexpr std::array<std::array<int, 8>, 8> kSpacing{{
        //  Ord Op Bin Rel Open Close Punct Inner
        {{0, 1, 5, 6, 0, 0, 0, 4}},      // Ord
        {{1, 1, -1, 6, 0, 0, 0, 4}},     // Op
        {{5, 5, -1, -1, 5, -1, -1, 5}},  // Bin
        {{6, 6, -1, 0, 6, 0, 0, 6}},     // Rel
        {{0, 0, -1, 0, 0, 0, 0, 0}},     // Open
        {{0, 1, 5, 6, 0, 0, 0, 4}},      // Close
        {{4, 4, -1, 4, 4, 4, 4, 4}},     // Punct
        {{4, 1, 5, 6, 4, 0, 4, 4}},      // Inner
}};

// In em; mu is 1/18 em.
float spacing_em(AtomClass left, AtomClass right, bool script_level) {
    int code = kSpacing[index_of(left)][index_of(right)];
    if (code >= 4) {
        if (script_level) {
            return 0.0f;
        }
        code -= 3;
    }
    switch (code) {
        case 1:
            return 3.0f / 18.0f;
        case 2:
            return 4.0f / 18.0f;
        case 3:
            return 5.0f / 18.0f;
        default:
            return 0.0f;
    }
}

class Layouter {
public:
    Layouter(const MathFont& font, float font_size, bool display)
        : font_(font),
          c_(font.constants()),
          upem_(font.units_per_em()),
          font_size_(font_size),
          root_style_{display ? 0 : 1, false} {}

    MathLayout run(const Row& root) {
        const Box box = layout_row(root, root_style_);
        MathLayout out;
        out.width = box.width;
        out.ascent = box.ascent;
        out.descent = box.descent;
        out.glyphs.reserve(box.glyphs.size());
        for (const BoxGlyph& g : box.glyphs) {
            out.glyphs.push_back({g.glyph, {g.x, box.ascent - g.y}, g.scale});
        }
        out.rules.reserve(box.rules.size());
        for (const BoxRule& r : box.rules) {
            out.rules.push_back({{r.x, box.ascent - (r.y + r.height)}, {r.width, r.height}});
        }
        return out;
    }

private:
    [[nodiscard]] float level_scale(int level) const {
        if (level >= 3) {
            return static_cast<float>(c_.script_script_percent_scale_down) / 100.0f;
        }
        if (level == 2) {
            return static_cast<float>(c_.script_percent_scale_down) / 100.0f;
        }
        return 1.0f;
    }

    // Em size of a style, in px.
    [[nodiscard]] float em(Style style) const {
        return font_size_ * level_scale(style.level);
    }

    // Font design units -> px in a style.
    [[nodiscard]] float unit(Style style) const {
        return em(style) / upem_;
    }

    // ---- glyph boxes -------------------------------------------------------------------------

    // One glyph, optionally lifted by `dy` (px).
    [[nodiscard]] Box glyph_box(GlyphId glyph, Style style, float dy = 0.0f) const {
        const float u = unit(style);
        const GlyphMetrics m = font_.metrics(glyph);
        Box box;
        box.glyphs.push_back({glyph, 0.0f, dy, u});
        box.width = m.advance * u;
        box.ascent = std::max(0.0f, m.y_max * u + dy);
        box.descent = std::max(0.0f, -(m.y_min * u + dy));
        if (const auto italic = font_.italics_correction(glyph)) {
            box.italic = *italic * u;
        }
        return box;
    }

    [[nodiscard]] Box glyph_run(const std::u32string& text, Style style) const {
        Box box;
        const float u = unit(style);
        for (const char32_t c : text) {
            const GlyphId glyph = font_.glyph_index(c);
            const GlyphMetrics m = font_.metrics(glyph);
            box.glyphs.push_back({glyph, box.width, 0.0f, u});
            box.ascent = std::max(box.ascent, m.y_max * u);
            box.descent = std::max(box.descent, -m.y_min * u);
            box.width += m.advance * u;
        }
        return box;
    }

    // A stretched glyph / assembly as a box. Its baseline is the first piece's origin, so ascent and descent
    // are the raw ink extents (descent can be negative until the caller lines the shape up).
    [[nodiscard]] static Box stretch_box(const StretchedGlyph& stretched, float u) {
        Box box;
        for (const StretchPiece& piece : stretched.pieces) {
            box.glyphs.push_back({piece.glyph, 0.0f, piece.y * u, u});
        }
        box.width = stretched.width * u;
        box.ascent = stretched.top * u;
        box.descent = -stretched.bottom * u;
        return box;
    }

    // ---- nodes -------------------------------------------------------------------------------

    [[nodiscard]] Box layout_node(const Node& node, Style style) const {
        return std::visit([&](const auto& value) { return layout(value, style); }, node.value);
    }

    [[nodiscard]] Box layout(const Symbol& symbol, Style style) const {
        const GlyphId glyph = font_.glyph_index(symbol.codepoint);
        if (symbol.math_class == MathClass::Op) {
            return large_operator(glyph, style);
        }
        return glyph_box(glyph, style);
    }

    // Display style takes the larger size variant, and every operator glyph is centred on the math axis.
    [[nodiscard]] Box large_operator(GlyphId glyph, Style style) const {
        GlyphId chosen = glyph;
        if (style.level == 0) {
            if (const GlyphConstruction* construction = font_.vertical_construction(glyph)) {
                if (!construction->variants.empty()) {
                    chosen = construction->variants.back().glyph;
                    for (const GlyphVariant& variant : construction->variants) {
                        if (variant.advance >= c_.display_operator_min_height) {
                            chosen = variant.glyph;
                            break;
                        }
                    }
                }
            }
        }
        const float u = unit(style);
        const GlyphMetrics m = font_.metrics(chosen);
        const float centre = (m.y_max + m.y_min) * 0.5f * u;
        return glyph_box(chosen, style, c_.axis_height * u - centre);
    }

    [[nodiscard]] Box layout(const Text& text, Style style) const {
        return glyph_run(text.text, style);
    }

    [[nodiscard]] Box layout(const OperatorName& name, Style style) const {
        return glyph_run(name.name, style);
    }

    [[nodiscard]] Box layout(const Space& space, Style style) const {
        Box box;
        box.width = space.em * em(style);
        return box;
    }

    [[nodiscard]] Box layout(const Group& group, Style style) const {
        return layout_row(group.items, style);
    }

    [[nodiscard]] Box layout(const Fraction& fraction, Style style) const {
        const bool display = style.level == 0;
        const Box numerator = layout_row(fraction.numerator, numerator_style(style));
        const Box denominator = layout_row(fraction.denominator, denominator_style(style));
        const float u = unit(style);
        const float axis = c_.axis_height * u;
        const float thickness = c_.fraction_rule_thickness * u;

        float numerator_shift =
                (display ? c_.fraction_numerator_display_style_shift_up : c_.fraction_numerator_shift_up) * u;
        float denominator_shift =
                (display ? c_.fraction_denominator_display_style_shift_down : c_.fraction_denominator_shift_down) *
                u;
        const float numerator_gap_min =
                (display ? c_.fraction_num_display_style_gap_min : c_.fraction_numerator_gap_min) * u;
        const float denominator_gap_min =
                (display ? c_.fraction_denom_display_style_gap_min : c_.fraction_denominator_gap_min) * u;

        // Clearance between the numerator's bottom and the bar's top, and the bar's bottom and the denominator's
        // top; push the parts apart when the nominal shifts leave less than the font asks for.
        const float numerator_gap = (numerator_shift - numerator.descent) - (axis + thickness * 0.5f);
        if (numerator_gap < numerator_gap_min) {
            numerator_shift += numerator_gap_min - numerator_gap;
        }
        const float denominator_gap = (axis - thickness * 0.5f) - (denominator.ascent - denominator_shift);
        if (denominator_gap < denominator_gap_min) {
            denominator_shift += denominator_gap_min - denominator_gap;
        }

        const float pad = kNullDelimiterEm * em(style);
        const float inner_width = std::max(numerator.width, denominator.width);
        Box box;
        place(box, numerator, pad + (inner_width - numerator.width) * 0.5f, numerator_shift);
        place(box, denominator, pad + (inner_width - denominator.width) * 0.5f, -denominator_shift);
        box.rules.push_back({pad, axis - thickness * 0.5f, inner_width, thickness});
        box.ascent = std::max(box.ascent, axis + thickness * 0.5f);
        box.descent = std::max(box.descent, -(axis - thickness * 0.5f));
        box.width = inner_width + 2.0f * pad;
        return box;
    }

    [[nodiscard]] Box layout(const Radical& radical, Style style) const {
        const float u = unit(style);
        const Box radicand = layout_row(radical.radicand, cramped_style(style));
        const float thickness = c_.radical_rule_thickness * u;
        const float gap = (style.level == 0 ? c_.radical_display_style_vertical_gap : c_.radical_vertical_gap) * u;

        // A radical sign tall enough for the radicand plus its gap and bar, centred around the radicand when
        // the sign overshoots (TeX rule 11).
        const float needed = radicand.ascent + radicand.descent + gap + thickness;
        const StretchedGlyph stretched = stretch_vertical(font_, kRadicalCodepoint, needed / u);
        Box sign = stretch_box(stretched, u);
        const float overshoot = sign.ascent + sign.descent - needed;
        const float clearance = gap + std::max(0.0f, overshoot * 0.5f);
        const float bar_top = radicand.ascent + clearance + thickness;
        shift_up(sign, bar_top - sign.ascent);  // sign's top stroke becomes the bar's top edge

        float sign_x = 0.0f;
        Box index;
        float index_x = 0.0f;
        float index_baseline = 0.0f;
        if (radical.index) {
            index = layout_row(*radical.index, Style{3, false});
            const float before = c_.radical_kern_before_degree * u;
            const float after = c_.radical_kern_after_degree * u;
            index_x = std::max(0.0f, before);
            sign_x = std::max(0.0f, before + index.width + after);
            const float raise = static_cast<float>(c_.radical_degree_bottom_raise_percent) / 100.0f *
                    (sign.ascent + sign.descent);
            index_baseline = -sign.descent + raise + index.descent;
        }

        Box box;
        place(box, sign, sign_x, 0.0f);
        const float body_x = sign_x + stretched.width * u;
        place(box, radicand, body_x, 0.0f);
        box.rules.push_back({body_x, bar_top - thickness, radicand.width, thickness});
        box.ascent = std::max(box.ascent, bar_top + c_.radical_extra_ascender * u);
        box.width = body_x + radicand.width;
        if (radical.index) {
            place(box, index, index_x, index_baseline);
        }
        return box;
    }

    // A combining mark over its base (`\vec E`). Horizontally the mark's own attachment point lands on the base's:
    // the MATH top-accent attachment of a lone glyph (italic capitals lean, so it is right of centre), else the
    // middle of the box. Vertically the mark keeps its designed position over an x-height base and is lifted
    // by however much taller the base is than `accent_base_height`, so it clears capitals and ascenders by the
    // same gap it leaves above lower-case letters. The box does not widen: an accent never pushes its neighbours.
    [[nodiscard]] Box layout(const Accent& accent, Style style) const {
        const Box base = layout_row(accent.base, cramped_style(style));
        const GlyphId mark = font_.glyph_index(accent.mark);
        if (mark == 0) {
            return base;  // the font lacks the mark: draw the bare base rather than a .notdef box
        }
        const float u = unit(style);

        float base_attach = base.width * 0.5f;
        if (accent.base.size() == 1) {
            if (const auto* symbol = std::get_if<Symbol>(&accent.base.front().value)) {
                if (const auto attach = font_.top_accent_attachment(font_.glyph_index(symbol->codepoint))) {
                    base_attach = *attach * u;
                }
            }
        }
        const GlyphMetrics mark_metrics = font_.metrics(mark);
        const float mark_attach =
                font_.top_accent_attachment(mark).value_or((mark_metrics.x_min + mark_metrics.x_max) * 0.5f) * u;
        const float lift = std::max(0.0f, base.ascent - c_.accent_base_height * u);

        Box box;
        place(box, base, 0.0f, 0.0f);
        place(box, glyph_box(mark, style, lift), base_attach - mark_attach, 0.0f);
        box.width = base.width;
        return box;
    }

    [[nodiscard]] Box layout(const Delimited& delimited, Style style) const {
        const Box body = layout_row(delimited.body, style);
        const float u = unit(style);
        const float axis = c_.axis_height * u;

        // TeX rule 19: size from the body's reach above and below the axis, with a little give.
        const float reach = std::max(body.ascent - axis, body.descent + axis);
        const float required = std::max(kDelimiterFactor * reach, 2.0f * reach - kDelimiterShortfallEm * em(style));

        const Box open = delimiter_box(delimited.open, required, style);
        const Box close = delimiter_box(delimited.close, required, style);
        Box box;
        float x = 0.0f;
        place(box, open, x, 0.0f);
        x += open.width;
        place(box, body, x, 0.0f);
        x += body.width;
        place(box, close, x, 0.0f);
        box.width = x + close.width;
        return box;
    }

    [[nodiscard]] Box delimiter_box(char32_t codepoint, float required, Style style) const {
        if (codepoint == 0) {
            Box box;
            box.width = kNullDelimiterEm * em(style);
            return box;
        }
        const float u = unit(style);
        Box box = stretch_box(stretch_vertical(font_, codepoint, required / u), u);
        shift_up(box, c_.axis_height * u - (box.ascent - box.descent) * 0.5f);  // centre on the math axis
        return box;
    }

    [[nodiscard]] Box layout(const Scripts& scripts, Style style) const {
        const Node* base_node = scripts.base.empty() ? nullptr : &scripts.base.front();
        const Box base = base_node != nullptr ? layout_node(*base_node, style) : Box{};

        bool takes_limits = false;
        bool limits_in_display = false;
        bool base_is_char = false;
        if (base_node != nullptr) {
            if (const auto* symbol = std::get_if<Symbol>(&base_node->value)) {
                takes_limits = symbol->math_class == MathClass::Op;
                limits_in_display = symbol->limits_in_display;
                base_is_char = !takes_limits;
            } else if (const auto* name = std::get_if<OperatorName>(&base_node->value)) {
                takes_limits = true;
                limits_in_display = name->limits_in_display;
            }
        }
        const bool stacked = takes_limits && scripts.limits != LimitsMode::NoLimits &&
                (scripts.limits == LimitsMode::Limits || (style.level == 0 && limits_in_display));

        const Style sup_style = script_style(style, style.cramped);
        const Style sub_style = script_style(style, true);
        const std::optional<Box> sup =
                scripts.superscript ? std::optional<Box>(layout_row(*scripts.superscript, sup_style)) : std::nullopt;
        const std::optional<Box> sub =
                scripts.subscript ? std::optional<Box>(layout_row(*scripts.subscript, sub_style)) : std::nullopt;

        const float u = unit(style);
        if (stacked) {
            return stack_limits(base, sub, sup, u);
        }

        // Drops are measured at the script size, shifts at the base size (TeX's sup_drop / sup1 split).
        const float u_script = unit(sup_style);
        float up = 0.0f;
        float down = 0.0f;
        if (!base_is_char) {
            up = base.ascent - c_.superscript_baseline_drop_max * u_script;
            down = base.descent + c_.subscript_baseline_drop_min * u_script;
        }
        if (sup) {
            const float nominal =
                    (style.cramped ? c_.superscript_shift_up_cramped : c_.superscript_shift_up) * u;
            up = std::max({up, nominal, sup->descent + c_.superscript_bottom_min * u});
        }
        if (sub && !sup) {
            down = std::max({down, c_.subscript_shift_down * u, sub->ascent - c_.subscript_top_max * u});
        } else if (sub && sup) {
            down = std::max(down, c_.subscript_shift_down * u);
            const float gap = (up - sup->descent) - (sub->ascent - down);
            const float gap_min = c_.sub_superscript_gap_min * u;
            if (gap < gap_min) {
                down += gap_min - gap;
                const float psi = c_.superscript_bottom_max_with_subscript * u - (up - sup->descent);
                if (psi > 0.0f) {
                    up += psi;
                    down -= psi;
                }
            }
        }

        // A superscript starts at the advance, a subscript is pulled back by the base's italic correction.
        const float space_after = c_.space_after_script * u;
        Box box;
        place(box, base, 0.0f, 0.0f);
        float width = base.width;
        if (sup) {
            place(box, *sup, base.width, up);
            width = std::max(width, base.width + sup->width + space_after);
        }
        if (sub) {
            const float sub_x = base.width - base.italic;
            place(box, *sub, sub_x, -down);
            width = std::max(width, sub_x + sub->width + space_after);
        }
        box.width = width;
        return box;
    }

    // Limits above and below a large operator (TeX rule 13a), centred on the operator's advance and nudged
    // apart by half its italic correction.
    [[nodiscard]] Box stack_limits(
            const Box& base, const std::optional<Box>& sub, const std::optional<Box>& sup, float u) const {
        const float centre_width = base.width;
        float width = centre_width;
        if (sup) {
            width = std::max(width, sup->width);
        }
        if (sub) {
            width = std::max(width, sub->width);
        }
        Box box;
        place(box, base, (width - centre_width) * 0.5f, 0.0f);
        if (sup) {
            const float gap =
                    std::max(c_.upper_limit_gap_min * u, c_.upper_limit_baseline_rise_min * u - sup->descent);
            place(box, *sup, (width - sup->width) * 0.5f + base.italic * 0.5f, base.ascent + gap + sup->descent);
        }
        if (sub) {
            const float gap =
                    std::max(c_.lower_limit_gap_min * u, c_.lower_limit_baseline_drop_min * u - sub->ascent);
            place(box, *sub, (width - sub->width) * 0.5f - base.italic * 0.5f, -(base.descent + gap + sub->ascent));
        }
        box.width = width;
        return box;
    }

    // ---- rows --------------------------------------------------------------------------------

    [[nodiscard]] AtomClass class_of(const Node& node) const {
        struct Visitor {
            AtomClass operator()(const Symbol& s) const {
                return to_atom_class(s.math_class);
            }
            AtomClass operator()(const Text&) const {
                return AtomClass::Ord;
            }
            AtomClass operator()(const OperatorName&) const {
                return AtomClass::Op;
            }
            AtomClass operator()(const Space&) const {
                return AtomClass::None;
            }
            AtomClass operator()(const Group&) const {
                return AtomClass::Ord;
            }
            AtomClass operator()(const Fraction&) const {
                return AtomClass::Inner;
            }
            AtomClass operator()(const Radical&) const {
                return AtomClass::Ord;
            }
            AtomClass operator()(const Accent&) const {
                return AtomClass::Ord;
            }
            AtomClass operator()(const Scripts& s) const {
                if (s.base.empty()) {
                    return AtomClass::Ord;
                }
                return std::visit(*this, s.base.front().value);
            }
            AtomClass operator()(const Delimited&) const {
                return AtomClass::Inner;
            }
        };
        return std::visit(Visitor{}, node.value);
    }

    [[nodiscard]] Box layout_row(const Row& row, Style style) const {
        struct Item {
            Box box;
            AtomClass atom_class = AtomClass::None;
        };
        std::vector<Item> items;
        items.reserve(row.size());
        for (const Node& node : row) {
            items.push_back({layout_node(node, style), class_of(node)});
        }

        // A binary operator with nothing sensible on its left, or about to be followed by a relation, closer or
        // punctuation, is really an ordinary symbol (unary minus, `(a+)`): TeX demotes it before spacing.
        AtomClass previous = AtomClass::None;
        for (Item& item : items) {
            if (item.atom_class == AtomClass::None) {
                continue;
            }
            if (item.atom_class == AtomClass::Bin &&
                    (previous == AtomClass::None || previous == AtomClass::Bin || previous == AtomClass::Op ||
                            previous == AtomClass::Rel || previous == AtomClass::Open ||
                            previous == AtomClass::Punct)) {
                item.atom_class = AtomClass::Ord;
            }
            previous = item.atom_class;
        }
        AtomClass next = AtomClass::None;
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            if (it->atom_class == AtomClass::None) {
                continue;
            }
            if (it->atom_class == AtomClass::Bin &&
                    (next == AtomClass::None || next == AtomClass::Rel || next == AtomClass::Close ||
                            next == AtomClass::Punct)) {
                it->atom_class = AtomClass::Ord;
            }
            next = it->atom_class;
        }

        const bool script_level = style.level >= 2;
        Box row_box;
        float x = 0.0f;
        previous = AtomClass::None;
        for (const Item& item : items) {
            if (item.atom_class != AtomClass::None) {
                if (previous != AtomClass::None) {
                    x += spacing_em(previous, item.atom_class, script_level) * em(style);
                }
                previous = item.atom_class;
            }
            place(row_box, item.box, x, 0.0f);
            x += item.box.width;
        }
        row_box.width = x;
        return row_box;
    }

    const MathFont& font_;
    const MathConstants& c_;
    float upem_;
    float font_size_;
    Style root_style_;
};

}

MathLayout layout_formula(const Row& row, const MathFont& font, const LayoutOptions& options) {
    return Layouter(font, options.font_size, options.display).run(row);
}

}
