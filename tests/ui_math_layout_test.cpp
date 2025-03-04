#include <gtest/gtest.h>

#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/math/math_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

using namespace engine::ui::math;

constexpr float kSize = 100.0f;           // px per em: 1 font unit = 0.1 px
constexpr float kU = kSize / 1000.0f;     // STIX Two Math has 1000 units per em
constexpr float kScriptScale = 0.70f;     // ScriptPercentScaleDown
constexpr float kScriptScriptScale = 0.55f;
constexpr float kEps = 0.02f;

const MathFont& stix() {
    static const MathFont font = [] {
        const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts" / "math.otf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto loaded = MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

const MathConstants& K() {
    return stix().constants();
}

MathLayout lay(std::string_view source, bool display = false, float size = kSize) {
    const ParseResult parsed = parse_formula(source);
    EXPECT_TRUE(parsed.ok()) << source;
    return layout_formula(parsed.root, stix(), LayoutOptions{size, display});
}

GlyphId id(char32_t codepoint) {
    return stix().glyph_index(codepoint);
}

// The `nth` placed glyph with this code point's glyph id.
const PlacedGlyph& glyph(const MathLayout& layout, char32_t codepoint, std::size_t nth = 0) {
    static const PlacedGlyph kMissing{};
    std::size_t seen = 0;
    for (const PlacedGlyph& g : layout.glyphs) {
        if (g.glyph == id(codepoint) && seen++ == nth) {
            return g;
        }
    }
    ADD_FAILURE() << "glyph U+" << std::hex << static_cast<unsigned>(codepoint) << " #" << nth << " not placed";
    return kMissing;
}

// A glyph's advance at the scale it was placed. In an OpenType math font this is the whole box: the advance
// already covers the glyph's overhang, the italic correction only pulls subscripts back.
float advance(char32_t codepoint, float scale) {
    return stix().metrics(id(codepoint)).advance * scale;
}

float italic(char32_t codepoint, float scale) {
    return stix().italics_correction(id(codepoint)).value_or(0.0f) * scale;
}

// Ink edges in the layout's y-down space.
float ink_top(const PlacedGlyph& g) {
    return g.origin.y - stix().metrics(g.glyph).y_max * g.scale;
}

float ink_bottom(const PlacedGlyph& g) {
    return g.origin.y - stix().metrics(g.glyph).y_min * g.scale;
}

constexpr char32_t kA = 0x1D44E;
constexpr char32_t kB = 0x1D44F;
constexpr char32_t kI = 0x1D456;
constexpr char32_t kN = 0x1D45B;
constexpr char32_t kX = 0x1D465;
constexpr char32_t kMinus = 0x2212;
constexpr char32_t kSum = 0x2211;
constexpr char32_t kInt = 0x222B;

}

TEST(MathLayout, EmptyFormulaHasNoSize) {
    const MathLayout l = lay("");
    EXPECT_FLOAT_EQ(l.width, 0.0f);
    EXPECT_FLOAT_EQ(l.ascent, 0.0f);
    EXPECT_FLOAT_EQ(l.descent, 0.0f);
    EXPECT_TRUE(l.glyphs.empty());
    EXPECT_TRUE(l.rules.empty());
}

TEST(MathLayout, SingleSymbolSitsOnTheBaseline) {
    const MathLayout l = lay("x");
    ASSERT_EQ(l.glyphs.size(), 1u);
    const GlyphMetrics m = stix().metrics(id(kX));
    EXPECT_EQ(l.glyphs[0].glyph, id(kX));
    EXPECT_NEAR(l.glyphs[0].scale, kU, 1e-6f);
    EXPECT_NEAR(l.glyphs[0].origin.x, 0.0f, kEps);
    EXPECT_NEAR(l.glyphs[0].origin.y, l.ascent, kEps);  // baseline is at y = ascent
    EXPECT_NEAR(l.ascent, m.y_max * kU, kEps);
    EXPECT_NEAR(l.descent, std::max(0.0f, -m.y_min * kU), kEps);
    EXPECT_NEAR(l.width, advance(kX, kU), kEps);
}

TEST(MathLayout, ScalesLinearlyWithFontSize) {
    for (const char* source : {"x", "a+b", "\\frac{a}{b}", "x^2_i", "\\sqrt{x+1}", "\\sum_{i=0}^n i", "\\left(x\\right)"}) {
        const MathLayout small = lay(source, false, 50.0f);
        const MathLayout large = lay(source, false, 100.0f);
        EXPECT_NEAR(large.width, small.width * 2.0f, 0.05f) << source;
        EXPECT_NEAR(large.ascent, small.ascent * 2.0f, 0.05f) << source;
        EXPECT_NEAR(large.descent, small.descent * 2.0f, 0.05f) << source;
        EXPECT_EQ(large.glyphs.size(), small.glyphs.size()) << source;
        EXPECT_EQ(large.rules.size(), small.rules.size()) << source;
    }
}

// ---- atom spacing ----------------------------------------------------------------------------

TEST(MathLayoutSpacing, BinaryOperatorGetsMediumSpaceEitherSide) {
    const MathLayout l = lay("a+b");
    const PlacedGlyph& plus = glyph(l, U'+');
    const PlacedGlyph& b = glyph(l, kB);
    EXPECT_NEAR(plus.origin.x, advance(kA, kU) + 4.0f / 18.0f * kSize, kEps);
    EXPECT_NEAR(b.origin.x, plus.origin.x + advance(U'+', kU) + 4.0f / 18.0f * kSize, kEps);
}

TEST(MathLayoutSpacing, RelationGetsThickSpace) {
    const MathLayout l = lay("a=b");
    const PlacedGlyph& eq = glyph(l, U'=');
    EXPECT_NEAR(eq.origin.x, advance(kA, kU) + 5.0f / 18.0f * kSize, kEps);
    EXPECT_NEAR(glyph(l, kB).origin.x, eq.origin.x + advance(U'=', kU) + 5.0f / 18.0f * kSize, kEps);
}

TEST(MathLayoutSpacing, LeadingMinusIsUnaryAndGetsNoSpace) {
    const MathLayout l = lay("-b");
    const PlacedGlyph& minus = glyph(l, kMinus);
    EXPECT_NEAR(minus.origin.x, 0.0f, kEps);
    EXPECT_NEAR(glyph(l, kB).origin.x, advance(kMinus, kU), kEps);
}

TEST(MathLayoutSpacing, BinaryBeforeARelationOrCloserIsDemoted) {
    // `a+=b` : the + has a relation on its right, so it is ordinary: no medium space after a.
    const MathLayout l = lay("a+=b");
    EXPECT_NEAR(glyph(l, U'+').origin.x, advance(kA, kU), kEps);
    // `(a+)` likewise before a closing parenthesis.
    const MathLayout paren = lay("(a+)");
    EXPECT_NEAR(glyph(paren, U'+').origin.x, advance(U'(', kU) + advance(kA, kU), kEps);
}

TEST(MathLayoutSpacing, PunctuationIsFollowedByThinSpace) {
    const MathLayout l = lay("a,b");
    const PlacedGlyph& comma = glyph(l, U',');
    EXPECT_NEAR(comma.origin.x, advance(kA, kU), kEps);  // Ord, Punct: none
    EXPECT_NEAR(glyph(l, kB).origin.x, comma.origin.x + advance(U',', kU) + 3.0f / 18.0f * kSize, kEps);
}

TEST(MathLayoutSpacing, ScriptStylesDropTheMediumAndThickSpaces) {
    const MathLayout l = lay("x^{a=b}");
    const PlacedGlyph& a = glyph(l, kA);
    const PlacedGlyph& eq = glyph(l, U'=');
    // Nothing between a's box and the relation: no thick space at script size.
    EXPECT_NEAR(eq.origin.x, a.origin.x + advance(kA, a.scale), kEps);
}

TEST(MathLayoutSpacing, OperatorNameIsFollowedByThinSpace) {
    const MathLayout l = lay("\\sin x");
    const PlacedGlyph& s = glyph(l, U's');
    const PlacedGlyph& i = glyph(l, U'i');
    const PlacedGlyph& n = glyph(l, U'n');
    EXPECT_NEAR(i.origin.x, s.origin.x + advance(U's', kU), kEps);
    EXPECT_NEAR(n.origin.x, i.origin.x + advance(U'i', kU), kEps);
    EXPECT_NEAR(glyph(l, kX).origin.x, n.origin.x + advance(U'n', kU) + 3.0f / 18.0f * kSize, kEps);
}

TEST(MathLayoutSpacing, ExplicitSpaceAddsItsWidthAndNothingElse) {
    const MathLayout l = lay("a\\,b");
    EXPECT_NEAR(glyph(l, kB).origin.x, advance(kA, kU) + 3.0f / 18.0f * kSize, kEps);
    const MathLayout quad = lay("a\\quad b");
    EXPECT_NEAR(glyph(quad, kB).origin.x, advance(kA, kU) + kSize, kEps);
    const MathLayout back = lay("a\\!b");
    EXPECT_NEAR(glyph(back, kB).origin.x, advance(kA, kU) - 3.0f / 18.0f * kSize, kEps);
}

// ---- fractions -------------------------------------------------------------------------------

TEST(MathLayoutFraction, BarSitsOnTheAxisWithTheFontsThickness) {
    const MathLayout l = lay("\\frac{a}{b}");
    ASSERT_EQ(l.rules.size(), 1u);
    const PlacedRule& bar = l.rules[0];
    EXPECT_NEAR(bar.size.y, K().fraction_rule_thickness * kU, kEps);
    const float bar_centre = bar.position.y + bar.size.y * 0.5f;
    EXPECT_NEAR(l.ascent - bar_centre, K().axis_height * kU, kEps);  // baseline is y = ascent
}

TEST(MathLayoutFraction, PartsAreScriptSizedAndCentredOverTheBar) {
    const MathLayout l = lay("\\frac{ab}{c}");
    const PlacedGlyph& a = glyph(l, kA);
    const PlacedGlyph& b = glyph(l, kB);
    const PlacedGlyph& c = glyph(l, 0x1D450);
    EXPECT_NEAR(a.scale, kU * kScriptScale, 1e-6f);
    EXPECT_NEAR(c.scale, kU * kScriptScale, 1e-6f);

    const PlacedRule& bar = l.rules[0];
    const float numerator_width = (b.origin.x + advance(kB, b.scale)) - a.origin.x;
    const float denominator_width = advance(0x1D450, c.scale);
    EXPECT_GT(numerator_width, denominator_width);
    EXPECT_NEAR(a.origin.x + numerator_width * 0.5f, bar.position.x + bar.size.x * 0.5f, kEps);
    EXPECT_NEAR(c.origin.x + denominator_width * 0.5f, bar.position.x + bar.size.x * 0.5f, kEps);
}

TEST(MathLayoutFraction, KeepsTheFontsMinimumGapsToTheBar) {
    for (const bool display : {false, true}) {
        const MathLayout l = lay("\\frac{a}{b}", display);
        const PlacedRule& bar = l.rules[0];
        const PlacedGlyph& a = glyph(l, kA);
        const PlacedGlyph& b = glyph(l, kB);
        const float u = kU;
        const float numerator_gap_min =
                (display ? K().fraction_num_display_style_gap_min : K().fraction_numerator_gap_min) * u;
        const float denominator_gap_min =
                (display ? K().fraction_denom_display_style_gap_min : K().fraction_denominator_gap_min) * u;
        EXPECT_GE(bar.position.y - ink_bottom(a), numerator_gap_min - kEps) << display;
        EXPECT_GE(ink_top(b) - (bar.position.y + bar.size.y), denominator_gap_min - kEps) << display;
    }
}

TEST(MathLayoutFraction, DisplayStyleRaisesTheNumeratorAndLowersTheDenominator) {
    const MathLayout text = lay("\\frac{a}{b}", false);
    const MathLayout display = lay("\\frac{a}{b}", true);
    const float text_up = text.ascent - glyph(text, kA).origin.y;
    const float display_up = display.ascent - glyph(display, kA).origin.y;
    EXPECT_GE(text_up, K().fraction_numerator_shift_up * kU - kEps);
    EXPECT_GE(display_up, K().fraction_numerator_display_style_shift_up * kU - kEps);
    EXPECT_GT(display_up, text_up);
    const float text_down = glyph(text, kB).origin.y - text.ascent;
    const float display_down = glyph(display, kB).origin.y - display.ascent;
    EXPECT_GE(text_down, K().fraction_denominator_shift_down * kU - kEps);
    EXPECT_GE(display_down, K().fraction_denominator_display_style_shift_down * kU - kEps);
}

TEST(MathLayoutFraction, DisplayFractionPartsStayAtTextSize) {
    // In display style a fraction's parts are text style (full size), in text style they are script style.
    const MathLayout display = lay("\\frac{a}{b}", true);
    EXPECT_NEAR(glyph(display, kA).scale, kU, 1e-6f);
    const MathLayout text = lay("\\frac{a}{b}", false);
    EXPECT_NEAR(glyph(text, kA).scale, kU * kScriptScale, 1e-6f);
}

TEST(MathLayoutFraction, NestedFractionShrinksToScriptScript) {
    const MathLayout l = lay("\\frac{1}{\\frac{a}{b}}");
    EXPECT_NEAR(glyph(l, U'1').scale, kU * kScriptScale, 1e-6f);
    EXPECT_NEAR(glyph(l, kA).scale, kU * kScriptScriptScale, 1e-6f);
    EXPECT_EQ(l.rules.size(), 2u);
}

// ---- scripts ---------------------------------------------------------------------------------

TEST(MathLayoutScripts, SuperscriptIsScriptSizedRaisedAndStartsAtTheAdvance) {
    const MathLayout l = lay("x^2");
    const PlacedGlyph& base = glyph(l, kX);
    const PlacedGlyph& sup = glyph(l, U'2');
    EXPECT_NEAR(sup.scale, kU * kScriptScale, 1e-6f);
    EXPECT_NEAR(sup.origin.x, advance(kX, kU), kEps);
    EXPECT_GE(base.origin.y - sup.origin.y, K().superscript_shift_up * kU - kEps);
}

TEST(MathLayoutScripts, SubscriptIsPulledBackByTheItalicCorrectionAndDropsByTheFontsShift) {
    const MathLayout l = lay("x_i");
    const PlacedGlyph& base = glyph(l, kX);
    const PlacedGlyph& sub = glyph(l, kI);
    EXPECT_NEAR(sub.scale, kU * kScriptScale, 1e-6f);
    // The subscript starts at the advance minus the base's italic correction; the superscript at the advance.
    EXPECT_GT(italic(kX, kU), 0.0f);
    EXPECT_NEAR(sub.origin.x, advance(kX, kU) - italic(kX, kU), kEps);
    EXPECT_GE(sub.origin.y - base.origin.y, K().subscript_shift_down * kU - kEps);
}

TEST(MathLayoutScripts, BothScriptsKeepTheMinimumGapBetweenThem) {
    const MathLayout l = lay("x_i^2");
    const PlacedGlyph& sup = glyph(l, U'2');
    const PlacedGlyph& sub = glyph(l, kI);
    EXPECT_GE(ink_top(sub) - ink_bottom(sup), K().sub_superscript_gap_min * kU - kEps);
    EXPECT_NEAR(sub.origin.x, advance(kX, kU) - italic(kX, kU), kEps);
    EXPECT_NEAR(sup.origin.x, advance(kX, kU), kEps);
}

TEST(MathLayoutScripts, ScriptsOfScriptsShrinkFurther) {
    const MathLayout l = lay("e^{x^2}");
    EXPECT_NEAR(glyph(l, kX).scale, kU * kScriptScale, 1e-6f);
    EXPECT_NEAR(glyph(l, U'2').scale, kU * kScriptScriptScale, 1e-6f);
}

TEST(MathLayoutScripts, CrampedStyleKeepsSuperscriptsLower) {
    const MathLayout free = lay("x^2");
    const float free_shift = glyph(free, kX).origin.y - glyph(free, U'2').origin.y;
    // A radicand is cramped, so its superscript uses the smaller cramped shift.
    const MathLayout cramped = lay("\\sqrt{x^2}");
    const float cramped_shift = glyph(cramped, kX).origin.y - glyph(cramped, U'2').origin.y;
    EXPECT_LT(cramped_shift, free_shift - 1.0f);
    EXPECT_GE(cramped_shift, K().superscript_shift_up_cramped * kU - kEps);
}

TEST(MathLayoutScripts, ScriptsOnATallBaseHangFromItsEdges) {
    // The base is not a lone glyph, so the script drops are measured against its real height.
    const MathLayout l = lay("\\left(\\frac{a}{b}\\right)^2");
    const PlacedGlyph& sup = glyph(l, U'2');
    EXPECT_LT(sup.origin.y, l.ascent - K().superscript_shift_up * kU);
}

TEST(MathLayoutScripts, PrimeIsASuperscript) {
    const MathLayout l = lay("f'");
    const PlacedGlyph& prime = glyph(l, 0x2032);
    EXPECT_NEAR(prime.scale, kU * kScriptScale, 1e-6f);
    EXPECT_LT(prime.origin.y, glyph(l, 0x1D453).origin.y);
}

// ---- large operators -------------------------------------------------------------------------

TEST(MathLayoutOperators, DisplayStyleTakesALargerSumAndCentresItOnTheAxis) {
    const MathLayout display = lay("\\sum", true);
    const MathLayout text = lay("\\sum", false);
    ASSERT_EQ(display.glyphs.size(), 1u);
    ASSERT_EQ(text.glyphs.size(), 1u);
    EXPECT_EQ(text.glyphs[0].glyph, id(kSum));
    EXPECT_NE(display.glyphs[0].glyph, id(kSum));  // the display variant
    EXPECT_GT(display.height(), text.height());

    for (const MathLayout* l : {&display, &text}) {
        const PlacedGlyph& g = l->glyphs[0];
        const float centre = (ink_top(g) + ink_bottom(g)) * 0.5f;
        EXPECT_NEAR(l->ascent - centre, K().axis_height * kU, kEps);
    }
}

TEST(MathLayoutOperators, DisplayLimitsStackAboveAndBelowCentredOnTheOperator) {
    const MathLayout l = lay("\\sum_{i}^{n}", true);
    const PlacedGlyph& sum = l.glyphs.front();
    const PlacedGlyph& sup = glyph(l, kN);
    const PlacedGlyph& sub = glyph(l, kI);
    const float sum_centre = sum.origin.x + stix().metrics(sum.glyph).advance * sum.scale * 0.5f;
    const auto box_centre = [](const PlacedGlyph& g) {
        return g.origin.x + stix().metrics(g.glyph).advance * g.scale * 0.5f;
    };
    EXPECT_NEAR(box_centre(sup), sum_centre, 0.05f);
    EXPECT_NEAR(box_centre(sub), sum_centre, 0.05f);
    EXPECT_LT(ink_bottom(sup), ink_top(sum));  // superscript above
    EXPECT_GT(ink_top(sub), ink_bottom(sum));  // subscript below
    EXPECT_GE(ink_top(sum) - ink_bottom(sup), K().upper_limit_gap_min * kU - kEps);
    EXPECT_GE(ink_top(sub) - ink_bottom(sum), K().lower_limit_gap_min * kU - kEps);
}

TEST(MathLayoutOperators, TextStyleSetsTheLimitsBesideTheOperator) {
    const MathLayout l = lay("\\sum_{i}^{n}", false);
    const PlacedGlyph& sum = l.glyphs.front();
    const PlacedGlyph& sup = glyph(l, kN);
    EXPECT_GE(sup.origin.x, sum.origin.x + stix().metrics(sum.glyph).advance * sum.scale - kEps);
}

TEST(MathLayoutOperators, IntegralKeepsItsLimitsBesideEvenInDisplayStyle) {
    const MathLayout l = lay("\\int_0^1", true);
    const PlacedGlyph& integral = l.glyphs.front();
    const PlacedGlyph& sup = glyph(l, U'1');
    EXPECT_GE(sup.origin.x, integral.origin.x + stix().metrics(integral.glyph).advance * integral.scale - 0.5f);
}

TEST(MathLayoutOperators, LimitsAndNolimitsOverrideTheDefault) {
    const MathLayout forced = lay("\\int\\limits_0^1", false);
    const PlacedGlyph& integral = forced.glyphs.front();
    const PlacedGlyph& sup = glyph(forced, U'1');
    EXPECT_LT(ink_bottom(sup), ink_top(integral));  // stacked even in text style

    const MathLayout beside = lay("\\sum\\nolimits_i^n", true);
    const PlacedGlyph& sum = beside.glyphs.front();
    EXPECT_GE(glyph(beside, kN).origin.x, sum.origin.x + stix().metrics(sum.glyph).advance * sum.scale - kEps);
}

TEST(MathLayoutOperators, LimOperatorNameStacksItsSubscriptInDisplayStyle) {
    const MathLayout display = lay("\\lim_{n}", true);
    const MathLayout text = lay("\\lim_{n}", false);
    const PlacedGlyph& l_display = glyph(display, U'l');
    EXPECT_GT(ink_top(glyph(display, kN)), ink_bottom(l_display));  // below the word
    const PlacedGlyph& m_text = glyph(text, U'm');
    EXPECT_GE(glyph(text, kN).origin.x, m_text.origin.x + advance(U'm', kU) - kEps);  // beside it
}

// ---- radicals --------------------------------------------------------------------------------

TEST(MathLayoutRadical, BarSpansTheRadicandAboveTheFontsGap) {
    const MathLayout l = lay("\\sqrt{x}");
    ASSERT_EQ(l.rules.size(), 1u);
    const PlacedRule& bar = l.rules[0];
    const PlacedGlyph& x = glyph(l, kX);
    EXPECT_NEAR(bar.size.y, K().radical_rule_thickness * kU, kEps);
    EXPECT_NEAR(bar.size.x, advance(kX, kU), kEps);
    EXPECT_NEAR(bar.position.x, x.origin.x, kEps);  // the bar starts where the radicand starts
    EXPECT_GE(ink_top(x) - (bar.position.y + bar.size.y), K().radical_vertical_gap * kU - kEps);
}

TEST(MathLayoutRadical, SignTopMeetsTheBar) {
    const MathLayout l = lay("\\sqrt{x}");
    const PlacedGlyph& sign = glyph(l, 0x221A);
    EXPECT_NEAR(ink_top(sign), l.rules[0].position.y, kEps);
}

TEST(MathLayoutRadical, TallRadicandGetsALargerSign) {
    const MathLayout small = lay("\\sqrt{x}");
    const MathLayout tall = lay("\\sqrt{\\frac{a}{b}}");
    const GlyphId base = id(0x221A);
    EXPECT_EQ(small.glyphs.front().glyph == base || small.glyphs.back().glyph == base, true);
    bool tall_has_base = false;
    for (const PlacedGlyph& g : tall.glyphs) {
        tall_has_base = tall_has_base || g.glyph == base;
    }
    EXPECT_FALSE(tall_has_base);
    // The sign still reaches the radical's own bar (the last rule: the fraction inside has one too).
    const PlacedGlyph& sign = tall.glyphs.front();
    EXPECT_NEAR(ink_top(sign), tall.rules.back().position.y, kEps);
    EXPECT_GE(tall.descent, 0.0f);
}

TEST(MathLayoutRadical, DisplayStyleLeavesALargerGap) {
    const MathLayout text = lay("\\sqrt{x}", false);
    const MathLayout display = lay("\\sqrt{x}", true);
    const float text_gap = ink_top(glyph(text, kX)) - (text.rules[0].position.y + text.rules[0].size.y);
    const float display_gap = ink_top(glyph(display, kX)) - (display.rules[0].position.y + display.rules[0].size.y);
    EXPECT_GT(display_gap, text_gap);
    EXPECT_GE(display_gap, K().radical_display_style_vertical_gap * kU - kEps);
}

TEST(MathLayoutRadical, IndexIsScriptScriptSizedAndLeftOfTheSign) {
    const MathLayout l = lay("\\sqrt[3]{x}");
    const PlacedGlyph& index = glyph(l, U'3');
    const PlacedGlyph& sign = glyph(l, 0x221A);
    EXPECT_NEAR(index.scale, kU * kScriptScriptScale, 1e-6f);
    EXPECT_NEAR(index.origin.x, K().radical_kern_before_degree * kU, kEps);
    // The index tucks into the sign's hook: the font's negative kern-after pulls the sign back under it.
    const float expected_sign_x = std::max(0.0f, K().radical_kern_before_degree * kU +
            advance(U'3', kU * kScriptScriptScale) + K().radical_kern_after_degree * kU);
    EXPECT_NEAR(sign.origin.x, expected_sign_x, kEps);
    EXPECT_GT(index.origin.x + advance(U'3', index.scale), sign.origin.x);
    // Raised: the index baseline sits above the sign's bottom.
    EXPECT_LT(index.origin.y, ink_bottom(sign));
}

TEST(MathLayoutRadical, NestedRadicalsStackTheirBars) {
    const MathLayout l = lay("\\sqrt{\\sqrt{x}}");
    ASSERT_EQ(l.rules.size(), 2u);
    EXPECT_NE(l.rules[0].position.y, l.rules[1].position.y);
}

// ---- \left ... \right ------------------------------------------------------------------------

TEST(MathLayoutDelimited, SizesDelimitersToTheBodyAndCentresThemOnTheAxis) {
    const MathLayout l = lay("\\left(\\frac{\\frac{a}{b}}{\\frac{c}{d}}\\right)");
    const PlacedGlyph& open = l.glyphs.front();
    const PlacedGlyph& close = l.glyphs.back();
    EXPECT_NE(open.glyph, id(U'('));  // grown past the base glyph
    EXPECT_NE(close.glyph, id(U')'));
    for (const PlacedGlyph* g : {&open, &close}) {
        const float centre = (ink_top(*g) + ink_bottom(*g)) * 0.5f;
        EXPECT_NEAR(l.ascent - centre, K().axis_height * kU, 0.1f);
    }
    // Both delimiters cover the body vertically.
    const PlacedRule& bar = l.rules[0];
    EXPECT_LT(ink_top(open), bar.position.y);
    EXPECT_GT(ink_bottom(open), bar.position.y);
}

TEST(MathLayoutDelimited, SmallBodyKeepsTheBaseGlyphs) {
    const MathLayout l = lay("\\left(x\\right)");
    EXPECT_EQ(l.glyphs.front().glyph, id(U'('));
    EXPECT_EQ(l.glyphs.back().glyph, id(U')'));
}

TEST(MathLayoutDelimited, NullDelimiterTakesOnlyItsFixedSpace) {
    const MathLayout l = lay("\\left. x \\right.");
    ASSERT_EQ(l.glyphs.size(), 1u);
    EXPECT_NEAR(l.glyphs[0].origin.x, 0.12f * kSize, kEps);
    EXPECT_NEAR(l.width, advance(kX, kU) + 2.0f * 0.12f * kSize, kEps);
}

TEST(MathLayoutDelimited, VeryTallBodiesAssembleTheDelimiter) {
    std::string tall = "x";
    for (int i = 0; i < 14; ++i) {
        tall = "\\frac{" + tall + "}{y}";
    }
    const MathLayout l = lay("\\left(" + tall + "\\right)");
    // More than one glyph piece per delimiter: bottom hook, extenders, top hook.
    const GlyphConstruction* c = stix().vertical_construction(id(U'('));
    ASSERT_NE(c, nullptr);
    const GlyphId bottom = c->assembly->parts.front().glyph;
    const GlyphId extender = c->assembly->parts[1].glyph;
    const auto count = [&](GlyphId g) {
        return std::count_if(l.glyphs.begin(), l.glyphs.end(), [&](const PlacedGlyph& p) { return p.glyph == g; });
    };
    EXPECT_EQ(count(bottom), 1);
    EXPECT_GE(count(extender), 1);
    EXPECT_FALSE(l.glyphs.empty());
}

TEST(MathLayoutDelimited, ScriptsAttachAfterTheClosingDelimiter) {
    const MathLayout l = lay("\\left(x\\right)^2");
    const PlacedGlyph& close = glyph(l, U')');
    EXPECT_GE(glyph(l, U'2').origin.x, close.origin.x + advance(U')', kU) - kEps);
}

// ---- accents ---------------------------------------------------------------------------------

namespace {

constexpr char32_t kVecMark = 0x20D7;  // COMBINING RIGHT ARROW ABOVE, what \vec draws
constexpr char32_t kCapitalE = 0x1D438;
constexpr char32_t kCapitalF = 0x1D439;
constexpr char32_t kCapitalA = 0x1D434;
constexpr char32_t kCapitalB = 0x1D435;

// Font units of the mark's own top-accent attachment, scaled the way the placed glyph is.
float mark_attach(const PlacedGlyph& mark) {
    return stix().top_accent_attachment(mark.glyph).value() * mark.scale;
}

}

TEST(MathLayoutAccent, LowercaseKeepsTheMarkAtItsDesignedHeight) {
    const MathLayout l = lay(R"(\vec{x})");
    ASSERT_EQ(l.glyphs.size(), 2u);
    const PlacedGlyph& x = glyph(l, kX);
    const PlacedGlyph& mark = glyph(l, kVecMark);
    EXPECT_NEAR(mark.scale, kU, 1e-6f);
    // x is shorter than the font's accent base height, so the mark is not lifted at all...
    EXPECT_NEAR(mark.origin.y, x.origin.y, kEps);
    // ...and floats above the letter, not on it.
    EXPECT_GT(ink_top(x) - ink_bottom(mark), 5.0f);
}

TEST(MathLayoutAccent, MarkIsCentredOnTheBaseGlyphsTopAccentAttachment) {
    const MathLayout l = lay(R"(\vec{x})");
    const PlacedGlyph& x = glyph(l, kX);
    const PlacedGlyph& mark = glyph(l, kVecMark);
    const float base_attach = stix().top_accent_attachment(x.glyph).value() * x.scale;
    EXPECT_NEAR(mark.origin.x + mark_attach(mark), x.origin.x + base_attach, kEps);

    // Italic capitals lean: their attachment point is right of the box's middle, and the mark follows it.
    const MathLayout capital = lay(R"(\vec{E})");
    const PlacedGlyph& e = glyph(capital, kCapitalE);
    const PlacedGlyph& e_mark = glyph(capital, kVecMark);
    const float e_attach = stix().top_accent_attachment(e.glyph).value() * e.scale;
    EXPECT_NEAR(e_mark.origin.x + mark_attach(e_mark), e.origin.x + e_attach, kEps);
    EXPECT_GT(e_attach, advance(kCapitalE, kU) * 0.5f);
}

TEST(MathLayoutAccent, CapitalsLiftTheMarkByTheirExtraHeight) {
    const MathLayout l = lay(R"(\vec{E})");
    const PlacedGlyph& e = glyph(l, kCapitalE);
    const PlacedGlyph& mark = glyph(l, kVecMark);
    const float lift = e.origin.y - mark.origin.y;
    const float cap_height = stix().metrics(e.glyph).y_max * kU;
    EXPECT_NEAR(lift, cap_height - K().accent_base_height * kU, kEps);
    EXPECT_GT(lift, 10.0f);
}

TEST(MathLayoutAccent, MarkClearsLowercaseAndCapitalsByTheSameGap) {
    const MathLayout small = lay(R"(\vec{x})");
    const MathLayout capital = lay(R"(\vec{F})");
    const float small_gap = ink_top(glyph(small, kX)) - ink_bottom(glyph(small, kVecMark));
    const float capital_gap = ink_top(glyph(capital, kCapitalF)) - ink_bottom(glyph(capital, kVecMark));
    EXPECT_NEAR(small_gap, capital_gap, 1.5f);
    EXPECT_GT(capital_gap, 5.0f);
}

TEST(MathLayoutAccent, BoxKeepsTheBaseWidthAndGrowsTallerToHoldTheMark) {
    const MathLayout bare = lay("E");
    const MathLayout accented = lay(R"(\vec{E})");
    EXPECT_NEAR(accented.width, bare.width, kEps);
    EXPECT_GT(accented.ascent, bare.ascent + 10.0f);
    // The box top is exactly the mark's top: the lift over the capital plus the mark's own height.
    const float lift = stix().metrics(id(kCapitalE)).y_max * kU - K().accent_base_height * kU;
    EXPECT_NEAR(accented.ascent, lift + stix().metrics(id(kVecMark)).y_max * kU, kEps);
    EXPECT_NEAR(ink_top(glyph(accented, kVecMark)), 0.0f, kEps);
    EXPECT_NEAR(accented.descent, bare.descent, kEps);
}

TEST(MathLayoutAccent, CompositeBaseIsCentredOnItsBox) {
    const MathLayout l = lay(R"(\vec{AB})");
    const PlacedGlyph& mark = glyph(l, kVecMark);
    EXPECT_NEAR(mark.origin.x + mark_attach(mark), l.width * 0.5f, kEps);
    EXPECT_NEAR(l.width, advance(kCapitalA, kU) + advance(kCapitalB, kU), kEps);
}

TEST(MathLayoutAccent, AccentedSymbolIsAnOrdinaryAtomForSpacing) {
    const MathLayout l = lay(R"(\vec{a}+\vec{b})");
    const PlacedGlyph& plus = glyph(l, U'+');
    // Ord, Bin: a medium space before the plus, measured from the accented a's box (the mark adds no width).
    EXPECT_NEAR(plus.origin.x, advance(kA, kU) + 4.0f / 18.0f * kSize, kEps);
}

TEST(MathLayoutAccent, ScriptsClearTheWholeAccentedSymbol) {
    const MathLayout l = lay(R"(\vec{F}^2)");
    const PlacedGlyph& sup = glyph(l, U'2');
    const PlacedGlyph& f = glyph(l, kCapitalF);
    EXPECT_NEAR(sup.origin.x, advance(kCapitalF, kU), kEps);
    EXPECT_GT(f.origin.y - sup.origin.y, K().superscript_shift_up * kU - kEps);
}

TEST(MathLayoutAccent, MarkShrinksWithTheStyleItIsSetIn) {
    const MathLayout l = lay(R"(x^{\vec{F}})");
    EXPECT_NEAR(glyph(l, kVecMark).scale, kU * kScriptScale, 1e-6f);
    EXPECT_NEAR(glyph(l, kCapitalF).scale, kU * kScriptScale, 1e-6f);
}

TEST(MathLayoutAccent, NestedAccentsStackUpwards) {
    const MathLayout l = lay(R"(\vec{\vec{x}})");
    ASSERT_EQ(l.glyphs.size(), 3u);
    const PlacedGlyph& inner = glyph(l, kVecMark, 0);
    const PlacedGlyph& outer = glyph(l, kVecMark, 1);
    EXPECT_LT(std::min(inner.origin.y, outer.origin.y), std::max(inner.origin.y, outer.origin.y) - 5.0f);
}

TEST(MathLayoutAccent, WorksInsideFractionsAndRadicals) {
    const MathLayout fraction = lay(R"(\frac{\vec{F}}{q})");
    EXPECT_EQ(fraction.glyphs.size(), 3u);
    EXPECT_EQ(fraction.rules.size(), 1u);
    const MathLayout radical = lay(R"(\sqrt{\vec{x}})");
    EXPECT_EQ(radical.glyphs.size(), 3u);
}

// ---- whole-formula invariants ----------------------------------------------------------------

// Every ink pixel lies in the layout's box (a little slack for the overhang a glyph's ink may have past its
// advance, e.g. the radical sign into the bar).
TEST(MathLayoutInvariants, EverythingIsInsideTheReportedBox) {
    const std::vector<std::string> formulas = {
            "x",
            "a+b=c",
            "\\frac{a}{b}",
            "x^2_i",
            "e^{x^2}",
            "\\sqrt{x+1}",
            "\\sqrt[3]{\\frac{a}{b}}",
            "\\sum_{i=0}^{n} i^2",
            "\\int_0^\\infty e^{-x^2}\\,\\mathrm{d}x",
            "\\left(\\frac{a}{b}\\right)^2",
            "\\lim_{x\\to0}\\frac{\\sin x}{x}",
            "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}",
            "\\left\\{\\frac{\\frac{a}{b}}{\\frac{c}{d}}\\right\\}",
            "\\vec{F} = m\\vec{a}",
            "\\nabla\\cdot\\vec{E} = \\frac{\\rho}{\\varepsilon_0}",
            "\\vec{x}_1^2 + \\sqrt{\\vec{AB}}",
    };
    for (const bool display : {false, true}) {
        for (const std::string& source : formulas) {
            const MathLayout l = lay(source, display);
            EXPECT_GT(l.width, 0.0f) << source;
            const float slack = 0.06f * kSize;
            for (const PlacedGlyph& g : l.glyphs) {
                const GlyphMetrics m = stix().metrics(g.glyph);
                EXPECT_GE(g.origin.x + m.x_min * g.scale, -slack) << source;
                EXPECT_LE(g.origin.x + m.x_max * g.scale, l.width + slack) << source;
                EXPECT_GE(ink_top(g), -slack) << source << " display=" << display;
                EXPECT_LE(ink_bottom(g), l.height() + slack) << source << " display=" << display;
            }
            for (const PlacedRule& r : l.rules) {
                EXPECT_GE(r.position.x, -kEps) << source;
                EXPECT_LE(r.position.x + r.size.x, l.width + slack) << source;
                EXPECT_GE(r.position.y, -slack) << source;
                EXPECT_LE(r.position.y + r.size.y, l.height() + slack) << source;
            }
        }
    }
}

TEST(MathLayoutInvariants, DisplayStyleNeverMakesAFormulaShorter) {
    for (const char* source : {"\\frac{a}{b}", "\\sum_{i=0}^{n} i", "\\sqrt{x}"}) {
        EXPECT_GE(lay(source, true).height(), lay(source, false).height() - kEps) << source;
    }
}
