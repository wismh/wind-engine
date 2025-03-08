#include <gtest/gtest.h>

#include "ui/math/math_font.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

using engine::ui::math::GlyphId;
using engine::ui::math::MathFont;
using engine::ui::math::MathFontError;
using engine::ui::math::OutlineCommand;

std::vector<std::uint8_t> read_builtin(const char* relative) {
    const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / relative;
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// STIX Two Math is ~800 KB; parse it once for the whole suite.
const MathFont& stix() {
    static const MathFont font = [] {
        const std::vector<std::uint8_t> bytes = read_builtin("fonts/math.otf");
        auto loaded = MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

constexpr char32_t kSum = 0x2211;
constexpr char32_t kRadical = 0x221A;
constexpr char32_t kIntegral = 0x222B;
constexpr char32_t kMathItalicF = 0x1D453;  // U+1D453 MATHEMATICAL ITALIC SMALL F
constexpr char32_t kRightArrow = 0x2192;

}

TEST(MathFont, LoadsBuiltinMathFont) {
    const MathFont& f = stix();
    EXPECT_FLOAT_EQ(f.units_per_em(), 1000.0f);
    EXPECT_GT(f.ascent(), 0.0f);
    EXPECT_LT(f.descent(), 0.0f);
}

TEST(MathFont, RejectsEmptyAndGarbageBytes) {
    const auto empty = MathFont::load({});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error(), MathFontError::InvalidFont);

    const std::vector<std::uint8_t> garbage(64, 0x00);
    const auto garbled = MathFont::load(garbage);
    ASSERT_FALSE(garbled.has_value());
    EXPECT_EQ(garbled.error(), MathFontError::InvalidFont);
}

TEST(MathFont, PlainTextFontHasNoMathTable) {
    const std::vector<std::uint8_t> bytes = read_builtin("fonts/ui.ttf");
    const auto loaded = MathFont::load(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), MathFontError::MissingMathTable);
}

TEST(MathFont, SurvivesMove) {
    const std::vector<std::uint8_t> bytes = read_builtin("fonts/math.otf");
    auto loaded = MathFont::load(bytes);
    ASSERT_TRUE(loaded.has_value());
    const GlyphId before = loaded->glyph_index(U'x');
    MathFont moved = std::move(*loaded);
    EXPECT_EQ(moved.glyph_index(U'x'), before);
    EXPECT_FALSE(moved.outline(before).empty());
}

// The 51 MathValueRecords are read positionally; pinning values from the start, middle and end of
// the record list catches a field-order slip that would silently shift everything after it.
TEST(MathFont, ReadsMathConstantsInSpecOrder) {
    const auto& c = stix().constants();
    EXPECT_EQ(c.script_percent_scale_down, 70);
    EXPECT_EQ(c.script_script_percent_scale_down, 55);
    EXPECT_EQ(c.radical_degree_bottom_raise_percent, 55);
    EXPECT_FLOAT_EQ(c.delimited_sub_formula_min_height, 1325.0f);
    EXPECT_FLOAT_EQ(c.display_operator_min_height, 1800.0f);
    EXPECT_FLOAT_EQ(c.math_leading, 150.0f);
    EXPECT_FLOAT_EQ(c.axis_height, 258.0f);
    EXPECT_FLOAT_EQ(c.accent_base_height, 480.0f);
    EXPECT_FLOAT_EQ(c.subscript_shift_down, 210.0f);
    EXPECT_FLOAT_EQ(c.superscript_shift_up, 360.0f);
    EXPECT_FLOAT_EQ(c.superscript_shift_up_cramped, 252.0f);
    EXPECT_FLOAT_EQ(c.upper_limit_gap_min, 135.0f);
    EXPECT_FLOAT_EQ(c.stack_gap_min, 150.0f);
    EXPECT_FLOAT_EQ(c.fraction_numerator_shift_up, 585.0f);
    EXPECT_FLOAT_EQ(c.fraction_numerator_display_style_shift_up, 640.0f);
    EXPECT_FLOAT_EQ(c.fraction_denominator_shift_down, 585.0f);
    EXPECT_FLOAT_EQ(c.fraction_rule_thickness, 68.0f);
    EXPECT_FLOAT_EQ(c.skewed_fraction_horizontal_gap, 350.0f);
    EXPECT_FLOAT_EQ(c.overbar_rule_thickness, 68.0f);
    EXPECT_FLOAT_EQ(c.radical_vertical_gap, 85.0f);
    EXPECT_FLOAT_EQ(c.radical_display_style_vertical_gap, 170.0f);
    EXPECT_FLOAT_EQ(c.radical_rule_thickness, 68.0f);
    EXPECT_FLOAT_EQ(c.radical_extra_ascender, 78.0f);
    EXPECT_FLOAT_EQ(c.radical_kern_before_degree, 65.0f);
    // Last MathValueRecord, and negative: proves the int16 sign extension too.
    EXPECT_FLOAT_EQ(c.radical_kern_after_degree, -335.0f);
    EXPECT_FLOAT_EQ(stix().min_connector_overlap(), 100.0f);
}

TEST(MathFont, MapsCodepointsToGlyphs) {
    const MathFont& f = stix();
    const GlyphId x = f.glyph_index(U'x');
    EXPECT_NE(x, 0);
    EXPECT_NE(x, f.glyph_index(U'y'));
    // No glyph for a codepoint outside every cmap range.
    EXPECT_EQ(f.glyph_index(char32_t{0x10FFFF}), 0);
}

TEST(MathFont, ReportsGlyphMetrics) {
    const MathFont& f = stix();
    const auto x = f.metrics(f.glyph_index(U'x'));
    EXPECT_FLOAT_EQ(x.advance, 479.0f);
    EXPECT_FLOAT_EQ(x.x_min, -2.0f);
    EXPECT_FLOAT_EQ(x.y_min, 0.0f);
    EXPECT_FLOAT_EQ(x.x_max, 482.0f);
    EXPECT_FLOAT_EQ(x.y_max, 473.0f);

    // A space has an advance but no ink.
    const auto space = f.metrics(f.glyph_index(U' '));
    EXPECT_GT(space.advance, 0.0f);
    EXPECT_FLOAT_EQ(space.x_max - space.x_min, 0.0f);
    EXPECT_FLOAT_EQ(space.y_max - space.y_min, 0.0f);
}

TEST(MathFont, ReadsOutlinesFromTheCffTable) {
    const MathFont& f = stix();
    const GlyphId x = f.glyph_index(U'x');
    const auto x_outline = f.outline(x);
    ASSERT_FALSE(x_outline.empty());
    EXPECT_EQ(x_outline.front().kind, OutlineCommand::Kind::Move);

    // Every point of the outline lies inside the glyph's own bounding box.
    const auto box = f.metrics(x);
    for (const OutlineCommand& command : x_outline) {
        EXPECT_GE(command.p.x, box.x_min);
        EXPECT_LE(command.p.x, box.x_max);
        EXPECT_GE(command.p.y, box.y_min);
        EXPECT_LE(command.p.y, box.y_max);
    }

    // CFF charstrings are cubic; a curved glyph must surface Cubic commands, not just lines.
    const auto f_outline = f.outline(f.glyph_index(kMathItalicF));
    EXPECT_TRUE(std::any_of(f_outline.begin(), f_outline.end(),
            [](const OutlineCommand& c) { return c.kind == OutlineCommand::Kind::Cubic; }));

    EXPECT_TRUE(f.outline(f.glyph_index(U' ')).empty());
}

TEST(MathFont, ReadsItalicsCorrectionAndTopAccentAttachment) {
    const MathFont& f = stix();
    const GlyphId italic_f = f.glyph_index(kMathItalicF);
    EXPECT_EQ(f.italics_correction(italic_f), 20.0f);
    EXPECT_EQ(f.top_accent_attachment(italic_f), 470.0f);
    EXPECT_EQ(f.italics_correction(f.glyph_index(kIntegral)), 230.0f);

    // Upright 'x' carries an accent anchor but no italics correction.
    const GlyphId x = f.glyph_index(U'x');
    EXPECT_FALSE(f.italics_correction(x).has_value());
    EXPECT_EQ(f.top_accent_attachment(x), 250.0f);
}

TEST(MathFont, ReadsVerticalVariantsSmallestFirst) {
    const MathFont& f = stix();
    const GlyphId paren = f.glyph_index(U'(');
    const auto* construction = f.vertical_construction(paren);
    ASSERT_NE(construction, nullptr);
    ASSERT_EQ(construction->variants.size(), 13u);
    EXPECT_EQ(construction->variants.front().glyph, paren);
    EXPECT_FLOAT_EQ(construction->variants.front().advance, 933.0f);
    EXPECT_FLOAT_EQ(construction->variants.back().advance, 3821.0f);
    EXPECT_TRUE(std::is_sorted(construction->variants.begin(), construction->variants.end(),
            [](const auto& a, const auto& b) { return a.advance < b.advance; }));

    // Large operators stretch through variants alone (no assembly to fall back on).
    const auto* sum = f.vertical_construction(f.glyph_index(kSum));
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->variants.size(), 2u);
    EXPECT_FALSE(sum->assembly.has_value());

    // A glyph that never stretches has no construction at all.
    EXPECT_EQ(f.vertical_construction(f.glyph_index(U'x')), nullptr);
}

TEST(MathFont, ReadsGlyphAssemblyWithOneExtender) {
    const MathFont& f = stix();
    const auto* radical = f.vertical_construction(f.glyph_index(kRadical));
    ASSERT_NE(radical, nullptr);
    EXPECT_EQ(radical->variants.size(), 4u);
    ASSERT_TRUE(radical->assembly.has_value());
    const auto& parts = radical->assembly->parts;
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(std::count_if(parts.begin(), parts.end(), [](const auto& p) { return p.extender; }), 1);
    EXPECT_TRUE(parts[1].extender);
    EXPECT_FLOAT_EQ(parts[1].full_advance, 651.0f);
    EXPECT_FLOAT_EQ(parts[1].start_connector_length, 650.0f);
    EXPECT_FLOAT_EQ(parts[0].full_advance, 1905.0f);

    // The parenthesis assembly runs bottom-to-top: bottom hook, extender, top hook.
    const auto* paren = f.vertical_construction(f.glyph_index(U'('));
    ASSERT_NE(paren, nullptr);
    ASSERT_TRUE(paren->assembly.has_value());
    ASSERT_EQ(paren->assembly->parts.size(), 3u);
    EXPECT_FALSE(paren->assembly->parts.front().extender);
    EXPECT_TRUE(paren->assembly->parts[1].extender);
    EXPECT_FALSE(paren->assembly->parts.back().extender);
}

TEST(MathFont, ReadsHorizontalConstructions) {
    const MathFont& f = stix();
    EXPECT_NE(f.horizontal_construction(f.glyph_index(kRightArrow)), nullptr);
    EXPECT_NE(f.horizontal_construction(f.glyph_index(char32_t{0x23DE})), nullptr);  // top curly bracket
    // Stretchy only along its own axis.
    EXPECT_EQ(f.horizontal_construction(f.glyph_index(kSum)), nullptr);
}
