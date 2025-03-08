#include <gtest/gtest.h>

#include "ui/math/math_font.h"
#include "ui/math/math_stretch.h"

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

using namespace engine::ui::math;

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

}

TEST(MathStretch, GlyphWithoutAConstructionComesBackAsItself) {
    const GlyphId x = stix().glyph_index(U'x');
    const StretchedGlyph s = stretch_vertical(stix(), U'x', 5000.0f);
    ASSERT_EQ(s.pieces.size(), 1u);
    EXPECT_EQ(s.pieces[0].glyph, x);
    EXPECT_FALSE(s.assembled);
    const GlyphMetrics m = stix().metrics(x);
    EXPECT_FLOAT_EQ(s.bottom, m.y_min);
    EXPECT_FLOAT_EQ(s.top, m.y_max);
    EXPECT_FLOAT_EQ(s.width, m.advance);
}

TEST(MathStretch, PicksTheSmallestVariantThatIsBigEnough) {
    const GlyphId paren = stix().glyph_index(U'(');
    const GlyphConstruction* c = stix().vertical_construction(paren);
    ASSERT_NE(c, nullptr);

    // Anything the base glyph already covers keeps the base glyph.
    EXPECT_EQ(stretch_vertical(stix(), U'(', 100.0f).pieces[0].glyph, paren);
    EXPECT_EQ(stretch_vertical(stix(), U'(', c->variants[0].advance).pieces[0].glyph, paren);

    // A hair over a variant's size needs the next one.
    EXPECT_EQ(stretch_vertical(stix(), U'(', c->variants[0].advance + 1.0f).pieces[0].glyph, c->variants[1].glyph);
    EXPECT_EQ(stretch_vertical(stix(), U'(', c->variants[1].advance).pieces[0].glyph, c->variants[1].glyph);
    EXPECT_EQ(stretch_vertical(stix(), U'(', c->variants[7].advance - 5.0f).pieces[0].glyph, c->variants[7].glyph);

    const StretchedGlyph biggest = stretch_vertical(stix(), U'(', c->variants.back().advance);
    ASSERT_EQ(biggest.pieces.size(), 1u);
    EXPECT_EQ(biggest.pieces[0].glyph, c->variants.back().glyph);
    EXPECT_FALSE(biggest.assembled);
}

TEST(MathStretch, VariantsGrowWithTheRequest) {
    float previous = 0.0f;
    for (float want = 500.0f; want <= 3800.0f; want += 250.0f) {
        const StretchedGlyph s = stretch_vertical(stix(), U'(', want);
        EXPECT_GE(s.size(), previous) << want;
        previous = s.size();
    }
}

TEST(MathStretch, BuildsAnAssemblyPastTheLargestVariant) {
    const GlyphConstruction* c = stix().vertical_construction(stix().glyph_index(U'('));
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(c->assembly.has_value());
    const std::vector<AssemblyPart>& parts = c->assembly->parts;
    ASSERT_EQ(parts.size(), 3u);

    const StretchedGlyph s = stretch_vertical(stix(), U'(', c->variants.back().advance + 1.0f);
    EXPECT_TRUE(s.assembled);
    // bottom hook, one or more extenders, top hook
    ASSERT_GE(s.pieces.size(), 3u);
    EXPECT_EQ(s.pieces.front().glyph, parts[0].glyph);
    EXPECT_EQ(s.pieces.back().glyph, parts[2].glyph);
    for (std::size_t i = 1; i + 1 < s.pieces.size(); ++i) {
        EXPECT_EQ(s.pieces[i].glyph, parts[1].glyph);
    }
}

// Each extender count covers a band of sizes (the overlap flexes between the font's minimum and what the
// connectors allow). A target inside a band is hit exactly; the 2-unit slack is the gap between a part's
// advance and its ink height.
TEST(MathStretch, AssemblyLandsOnTheRequestedSizeWhenTheOverlapCanReachIt) {
    for (const float want : {4500.0f, 5500.0f, 6600.0f, 7500.0f, 9000.0f, 15000.0f}) {
        const StretchedGlyph s = stretch_vertical(stix(), U'(', want);
        ASSERT_TRUE(s.assembled) << want;
        EXPECT_GE(s.size(), want - 2.0f) << want;
        EXPECT_LE(s.size(), want + 2.0f) << want;
    }
}

// Between two bands no overlap reaches the target, so the stack settles on the smaller band's upper edge:
// never short of the request, never by more than the extender it had to add.
TEST(MathStretch, AssemblyNeverFallsShortAndOvershootsByLessThanAnExtender) {
    const GlyphConstruction* c = stix().vertical_construction(stix().glyph_index(U'('));
    ASSERT_NE(c, nullptr);
    const float extender = c->assembly->parts[1].full_advance;
    for (const float want : {4000.0f, 6000.0f, 8250.0f}) {
        const StretchedGlyph s = stretch_vertical(stix(), U'(', want);
        ASSERT_TRUE(s.assembled) << want;
        EXPECT_GE(s.size(), want - 2.0f) << want;
        EXPECT_LE(s.size(), want + extender) << want;
    }
}

TEST(MathStretch, AssemblyOverlapsStayWithinWhatTheConnectorsAllow) {
    const GlyphConstruction* c = stix().vertical_construction(stix().glyph_index(U'('));
    ASSERT_NE(c, nullptr);
    const std::vector<AssemblyPart>& parts = c->assembly->parts;
    const StretchedGlyph s = stretch_vertical(stix(), U'(', 8000.0f);
    ASSERT_TRUE(s.assembled);
    ASSERT_GE(s.pieces.size(), 3u);

    const auto part_for = [&](GlyphId glyph) -> const AssemblyPart& {
        return *std::find_if(parts.begin(), parts.end(), [&](const AssemblyPart& p) { return p.glyph == glyph; });
    };
    for (std::size_t i = 0; i + 1 < s.pieces.size(); ++i) {
        const AssemblyPart& lower = part_for(s.pieces[i].glyph);
        const AssemblyPart& upper = part_for(s.pieces[i + 1].glyph);
        const float lower_edge = s.pieces[i].y + stix().metrics(lower.glyph).y_min;
        const float upper_edge = s.pieces[i + 1].y + stix().metrics(upper.glyph).y_min;
        const float overlap = lower.full_advance - (upper_edge - lower_edge);
        EXPECT_GE(overlap, stix().min_connector_overlap() - 0.01f) << i;
        EXPECT_LE(overlap, std::min(lower.end_connector_length, upper.start_connector_length) + 0.01f) << i;
        EXPECT_GT(s.pieces[i + 1].y, s.pieces[i].y) << i;
    }
}

TEST(MathStretch, AssemblyAlsoCoversTheRadicalSign) {
    const GlyphConstruction* c = stix().vertical_construction(stix().glyph_index(char32_t{0x221A}));
    ASSERT_NE(c, nullptr);
    const StretchedGlyph s = stretch_vertical(stix(), 0x221A, c->variants.back().advance * 2.0f);
    EXPECT_TRUE(s.assembled);
    EXPECT_GE(s.size(), c->variants.back().advance * 2.0f - 2.0f);
}

TEST(MathStretch, OperatorWithOnlyVariantsSettlesForTheLargest) {
    const GlyphId sum = stix().glyph_index(char32_t{0x2211});
    const GlyphConstruction* c = stix().vertical_construction(sum);
    ASSERT_NE(c, nullptr);
    ASSERT_FALSE(c->assembly.has_value());
    const StretchedGlyph s = stretch_vertical(stix(), 0x2211, 99999.0f);
    ASSERT_EQ(s.pieces.size(), 1u);
    EXPECT_EQ(s.pieces[0].glyph, c->variants.back().glyph);
    EXPECT_FALSE(s.assembled);
}
