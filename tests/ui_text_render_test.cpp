#include <gtest/gtest.h>

#include "ui/math/math_font.h"
#include "ui/render/font_face.h"
#include "ui/render/font_library.h"
#include "ui/render/glyph_atlas.h"
#include "ui/render/skyline_packer.h"
#include "ui/render/text_layout.h"
#include "ui/text_wrap.h"

#include <engine/builtin_ids.h>
#include <engine/resources/font.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

using engine::AssetId;
using engine::Font;
using engine::ui::AtlasGlyph;
using engine::ui::AtlasPage;
using engine::ui::AtlasRect;
using engine::ui::FontFace;
using engine::ui::FontLibrary;
using engine::ui::GlyphAtlas;
using engine::ui::GlyphQuad;
using engine::ui::SkylinePacker;
using engine::ui::TextBlock;
using engine::ui::UiAlign;

Font read_builtin(const char* relative) {
    const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / relative;
    std::ifstream in(path, std::ios::binary);
    Font font;
    font.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return font;
}

const Font& ui_font() {
    static const Font font = read_builtin("fonts/ui.ttf");
    return font;
}

FontFace load_face() {
    auto face = FontFace::load(ui_font().bytes);
    EXPECT_TRUE(face.has_value());
    return std::move(*face);
}

// The pen advance of one glyph at `isize`, as fontstash rounds it.
float glyph_advance(FontFace& face, std::uint32_t codepoint, int isize) {
    const auto& metrics = face.glyph_metrics(face.glyph_index(codepoint), isize);
    return static_cast<float>(static_cast<int>(static_cast<float>(metrics.xadv) / 10.0f + 0.5f));
}

// The whole-pixel kerning fontstash adds between two glyphs.
float kern_pixels(FontFace& face, std::uint32_t left, std::uint32_t right, int isize) {
    const float adv =
            static_cast<float>(face.kern_advance(face.glyph_index(left), face.glyph_index(right))) * face.scale(isize);
    return static_cast<float>(static_cast<int>(adv + 0.5f));
}

bool overlaps(const AtlasRect& a, const AtlasRect& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

std::vector<GlyphQuad> layout(FontFace& face, GlyphAtlas& atlas, std::string_view text, float size, glm::vec2 at,
        UiAlign h, UiAlign v, float raster_scale = 1.0f) {
    std::vector<GlyphQuad> quads;
    engine::ui::layout_glyphs(face, 0, atlas, text, size, at, h, v, raster_scale, quads);
    return quads;
}

}

TEST(UiTextRender, LoadsTheBuiltinUiFontAndRejectsGarbage) {
    EXPECT_TRUE(FontFace::load(ui_font().bytes).has_value());
    EXPECT_FALSE(FontFace::load({}).has_value());
    const std::vector<std::uint8_t> garbage(64, 0);
    EXPECT_FALSE(FontFace::load(garbage).has_value());
}

TEST(UiTextRender, MetricsArePositiveAndAddUp) {
    FontFace face = load_face();
    EXPECT_GT(face.ascender(), 0.0f);
    EXPECT_LT(face.descender(), 0.0f);
    EXPECT_FLOAT_EQ(face.line_height(), face.ascender() - face.descender());
    for (const float size : {9.0f, 12.0f, 16.0f, 24.0f, 40.0f}) {
        const auto m = engine::ui::text_metrics(face, size);
        EXPECT_GT(m.ascent, 0.0f);
        EXPECT_GT(m.descent, 0.0f);
        EXPECT_NEAR(m.ascent + m.descent, m.line_height, 1e-3f);
        // fontstash normalizes by the whole height, so the line height is the size itself.
        EXPECT_NEAR(m.line_height, size, 1e-3f);
    }
}

TEST(UiTextRender, SizesQuantizeToTenthsOfAPixel) {
    FontFace face = load_face();
    EXPECT_EQ(engine::ui::quantize_text_size(13.37f), 133);
    EXPECT_EQ(engine::ui::quantize_text_size(16.0f), 160);
    EXPECT_EQ(engine::ui::quantize_text_size(0.05f), 0);
    const auto a = engine::ui::text_metrics(face, 13.37f);
    const auto b = engine::ui::text_metrics(face, 13.3f);
    EXPECT_FLOAT_EQ(a.ascent, b.ascent);
    EXPECT_FLOAT_EQ(a.line_height, b.line_height);
    EXPECT_EQ(engine::ui::text_extent(face, "Quantized", 13.37f), engine::ui::text_extent(face, "Quantized", 13.3f));
    // Degenerate sizes take IUiPainter's 80/20 split, as the NanoVG painter fell back to it.
    const auto tiny = engine::ui::text_metrics(face, 0.05f);
    EXPECT_FLOAT_EQ(tiny.line_height, 0.05f);
    EXPECT_FLOAT_EQ(tiny.ascent, 0.05f * 0.8f);
}

TEST(UiTextRender, RasterScaleQuantizesLikeNanoVg) {
    EXPECT_FLOAT_EQ(engine::ui::text_raster_scale(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(engine::ui::text_raster_scale(1.234f), 1.23f);
    EXPECT_FLOAT_EQ(engine::ui::text_raster_scale(9.0f), 4.0f);
    EXPECT_FLOAT_EQ(engine::ui::text_raster_scale(0.001f), 0.0f);
    EXPECT_FLOAT_EQ(engine::ui::text_raster_scale(1.0f, 2.0f), 2.0f);
}

TEST(UiTextRender, MeasuresAdvanceAndLineHeight) {
    FontFace face = load_face();
    const float line = engine::ui::text_metrics(face, 16.0f).line_height;
    const glm::vec2 empty = engine::ui::text_extent(face, "", 16.0f);
    EXPECT_FLOAT_EQ(empty.x, 0.0f);
    EXPECT_FLOAT_EQ(empty.y, line);

    const glm::vec2 hello = engine::ui::text_extent(face, "Hello, world", 16.0f);
    EXPECT_GT(hello.x, 50.0f);
    EXPECT_FLOAT_EQ(hello.y, line);
    // Every step of the pen is whole pixels.
    EXPECT_FLOAT_EQ(hello.x, std::floor(hello.x));
    // A trailing space counts: the advance, not the ink box.
    EXPECT_GT(engine::ui::text_extent(face, "Hello, world ", 16.0f).x, hello.x);
    // NanoVG measured a C string: a NUL ends it.
    EXPECT_EQ(engine::ui::text_extent(face, std::string_view("ab\0cd", 5), 16.0f).x,
            engine::ui::text_extent(face, "ab", 16.0f).x);
}

TEST(UiTextRender, MeasureIsSumOfAdvancesPlusKerning) {
    FontFace face = load_face();
    const int isize = 240;
    const std::string text = "AVATAR Typo, Wolf.";
    float expected = 0.0f;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i > 0) {
            expected += kern_pixels(face, static_cast<unsigned char>(text[i - 1]), static_cast<unsigned char>(text[i]),
                    isize);
        }
        expected += glyph_advance(face, static_cast<unsigned char>(text[i]), isize);
    }
    EXPECT_FLOAT_EQ(engine::ui::text_extent(face, text, 24.0f).x, expected);

    // Joining two runs adds the kerning of the pair at the seam, nothing else. stb_truetype 1.24 reads only plain
    // GPOS pair lookups (not extension lookups, where Inter keeps most of its kerning), so look for a pair it kerns.
    std::string pair;
    for (char l = 33; l < 127 && pair.empty(); ++l) {
        for (char r = 33; r < 127; ++r) {
            if (kern_pixels(face, static_cast<unsigned char>(l), static_cast<unsigned char>(r), isize) != 0.0f) {
                pair = {l, r};
                break;
            }
        }
    }
    for (const std::string seam : {std::string("AV"), pair}) {
        if (seam.empty()) {
            continue;
        }
        const float joined = engine::ui::text_extent(face, seam, 24.0f).x;
        const float left = engine::ui::text_extent(face, seam.substr(0, 1), 24.0f).x;
        const float right = engine::ui::text_extent(face, seam.substr(1, 1), 24.0f).x;
        EXPECT_FLOAT_EQ(joined, left + right + kern_pixels(face, static_cast<unsigned char>(seam[0]),
                                                       static_cast<unsigned char>(seam[1]), isize))
                << seam;
    }
    EXPECT_FALSE(pair.empty()) << "no ASCII pair of the UI font kerns through stb_truetype";
}

TEST(UiTextRender, DecodesUtf8LikeFontstash) {
    FontFace face = load_face();
    const int isize = 160;
    // U+00E9 is two bytes, U+20AC three.
    EXPECT_FLOAT_EQ(engine::ui::text_extent(face, "\xC3\xA9", 16.0f).x, glyph_advance(face, 0xE9, isize));
    EXPECT_NE(face.glyph_index(0xE9), 0);
    EXPECT_FLOAT_EQ(engine::ui::text_extent(face, "\xE2\x82\xAC", 16.0f).x, glyph_advance(face, 0x20AC, isize));
    // A malformed byte parks the decoder for the rest of the string: nothing after it is measured.
    const float ab = engine::ui::text_extent(face, "ab", 16.0f).x;
    EXPECT_FLOAT_EQ(engine::ui::text_extent(face, "ab\xFF" "cd", 16.0f).x, ab);
    // A code point the font lacks measures as .notdef rather than vanishing.
    EXPECT_EQ(face.glyph_index(0x10FFFD), 0);
    EXPECT_GT(engine::ui::text_extent(face, "\xF4\x8F\xBF\xBD", 16.0f).x, 0.0f);
}

TEST(UiTextRender, BreakLinesFollowsNanoVgRows) {
    FontFace face = load_face();
    const auto rows = [&](std::string_view text, float width) {
        return engine::ui::wrap_text(face, text, 16.0f, width).lines;
    };
    EXPECT_TRUE(rows("", 100.0f).empty());
    EXPECT_TRUE(rows("   ", 100.0f).empty());

    const auto one = rows("hello world", 1000.0f);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].begin, 0u);
    EXPECT_EQ(one[0].end, 11u);
    EXPECT_FLOAT_EQ(one[0].width, engine::ui::text_extent(face, "hello world", 16.0f).x);

    // A newline always ends a row; an empty line stays a row; \r\n is one break.
    const auto lines = rows("a\n\nb\r\nc", 1000.0f);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[1].begin, lines[1].end);
    EXPECT_EQ(lines[2].begin, 3u);
    EXPECT_EQ(lines[3].begin, 6u);

    // Leading spaces are skipped, trailing ones are not part of the row.
    const auto lead = rows("   hello   ", 1000.0f);
    ASSERT_EQ(lead.size(), 1u);
    EXPECT_EQ(lead[0].begin, 3u);
    EXPECT_EQ(lead[0].end, 8u);

    // A word wider than the row is split, at least one character per row.
    // NanoVG quirk kept for parity: a row cut inside a word reports the width up to and including the character
    // that did not fit (rowWidth is updated before the overflow check), so it is wider than `max_width`.
    const std::string word = "abcdefghijklmnop";
    const auto split = rows(word, 30.0f);
    ASSERT_GT(split.size(), 2u);
    for (std::size_t r = 0; r < split.size(); ++r) {
        const auto& row = split[r];
        EXPECT_LT(row.begin, row.end);
        const bool last = r + 1 == split.size();
        const std::string_view counted(word.data() + row.begin, row.end - row.begin + (last ? 0 : 1));
        EXPECT_FLOAT_EQ(row.width, engine::ui::text_extent(face, counted, 16.0f).x) << r;
        if (!last) {
            EXPECT_GT(row.width, 30.0f);
            EXPECT_LE(engine::ui::text_extent(face, word.substr(row.begin, row.end - row.begin), 16.0f).x, 30.0f);
        }
    }
    EXPECT_EQ(split.back().end, 16u);
    const auto narrow = rows("abc", 0.0f);
    EXPECT_EQ(narrow.size(), 3u);

    const TextBlock block = engine::ui::wrap_text(face, "x", 16.0f, 10.0f);
    EXPECT_FLOAT_EQ(block.line_height, engine::ui::text_metrics(face, 16.0f).line_height);
}

TEST(UiTextRender, BreakLinesAgreesWithBreakTextLines) {
    FontFace face = load_face();
    const auto width_of = [&](std::string_view slice) { return engine::ui::text_extent(face, slice, 16.0f).x; };
    const std::vector<std::string> texts = {
            "The quick brown fox jumps over the lazy dog",
            "  leading spaces and   inner   runs  ",
            "line one\nline two\n\nline four",
            "supercalifragilisticexpialidocious is long",
            "tabs\tbetween\twords and a very long tail of text",
    };
    for (const std::string& text : texts) {
        for (const float width : {40.0f, 75.0f, 120.0f, 200.0f, 1000.0f}) {
            const auto ours = engine::ui::wrap_text(face, text, 16.0f, width).lines;
            const auto reference = engine::ui::break_text_lines(text, width, width_of);
            ASSERT_EQ(ours.size(), reference.size()) << text << " @ " << width;
            for (std::size_t i = 0; i < ours.size(); ++i) {
                EXPECT_EQ(ours[i].begin, reference[i].begin) << text << " @ " << width << " row " << i;
                EXPECT_EQ(ours[i].end, reference[i].end) << text << " @ " << width << " row " << i;
                // A row cut inside a word counts the character that overflowed (see BreakLinesFollowsNanoVgRows).
                const bool cut = ours[i].end < text.size() && text[ours[i].end] != ' ' &&
                                 text[ours[i].end] != '\t' && text[ours[i].end] != '\n';
                const float expected =
                        cut ? width_of(std::string_view(text).substr(ours[i].begin, ours[i].end - ours[i].begin + 1))
                            : reference[i].width;
                EXPECT_FLOAT_EQ(ours[i].width, expected) << text << " @ " << width << " row " << i;
            }
        }
    }
}

TEST(UiTextRender, BreakLinesSplitsMultibyteWordsOnCodePoints) {
    FontFace face = load_face();
    const std::string text = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";  // eight U+00E9
    const auto lines = engine::ui::wrap_text(face, text, 16.0f, 20.0f).lines;
    ASSERT_GT(lines.size(), 1u);
    for (const auto& line : lines) {
        EXPECT_EQ(line.begin % 2, 0u);
        EXPECT_EQ(line.end % 2, 0u);
    }
    EXPECT_EQ(lines.back().end, text.size());
}

TEST(UiTextRender, BreakLinesChunksRowsLikeTheOldPainter) {
    FontFace face = load_face();
    // More than 32 rows: the painter re-entered nvgTextBreakLines at row 33, and every row is still there.
    std::string text;
    for (int i = 0; i < 40; ++i) {
        text += "row\n";
    }
    const auto lines = engine::ui::wrap_text(face, text, 16.0f, 1000.0f).lines;
    ASSERT_EQ(lines.size(), 40u);
    EXPECT_EQ(lines[39].begin, 39u * 4u);
}

TEST(UiTextRender, SkylinePackerNeverOverlaps) {
    SkylinePacker packer(64, 64);
    std::vector<AtlasRect> placed;
    for (int i = 0; i < 200; ++i) {
        const int w = 3 + (i * 7) % 11;
        const int h = 4 + (i * 5) % 9;
        const auto at = packer.add(w, h);
        if (!at) {
            continue;
        }
        const AtlasRect rect{at->x, at->y, at->x + w, at->y + h};
        EXPECT_GE(rect.x0, 0);
        EXPECT_GE(rect.y0, 0);
        EXPECT_LE(rect.x1, 64);
        EXPECT_LE(rect.y1, 64);
        for (const AtlasRect& other : placed) {
            EXPECT_FALSE(overlaps(rect, other));
        }
        placed.push_back(rect);
    }
    EXPECT_GT(placed.size(), 30u);
    EXPECT_FALSE(packer.add(65, 1).has_value());
    EXPECT_FALSE(packer.add(0, 4).has_value());
}

TEST(UiTextRender, AtlasCachesPacksAndGrows) {
    FontFace face = load_face();
    GlyphAtlas atlas(64);
    EXPECT_EQ(atlas.page_count(), 0u);
    EXPECT_EQ(atlas.version(), 0u);

    const int isize = 400;
    const auto& h = face.glyph_metrics(face.glyph_index('H'), isize);
    const AtlasGlyph& first = atlas.glyph(face, 0, h, isize);
    EXPECT_EQ(atlas.page_count(), 1u);
    EXPECT_EQ(atlas.version(), 1u);
    EXPECT_EQ(first.cell.x1 - first.cell.x0, h.bitmap_width() + 2 * GlyphAtlas::kPadding);
    EXPECT_EQ(first.cell.y1 - first.cell.y0, h.bitmap_height() + 2 * GlyphAtlas::kPadding);
    EXPECT_EQ(atlas.page(0).dirty_since(0), first.cell);
    EXPECT_TRUE(atlas.page(0).dirty_since(1).empty());

    // A second request is the cached cell; nothing is written.
    const AtlasGlyph& again = atlas.glyph(face, 0, h, isize);
    EXPECT_EQ(&again, &first);
    EXPECT_EQ(atlas.version(), 1u);
    EXPECT_EQ(atlas.glyph_count(), 1u);
    // Another face id is another glyph.
    (void)atlas.glyph(face, 1, h, isize);
    EXPECT_EQ(atlas.glyph_count(), 2u);

    // The cell's two-pixel border is clear and the bitmap has ink.
    const AtlasPage& page = atlas.page(first.page);
    const auto pixel = [&](int x, int y) {
        return page.pixels()[static_cast<std::size_t>(y) * static_cast<std::size_t>(page.width()) +
                             static_cast<std::size_t>(x)];
    };
    int ink = 0;
    for (int y = first.cell.y0; y < first.cell.y1; ++y) {
        for (int x = first.cell.x0; x < first.cell.x1; ++x) {
            const bool border = x < first.cell.x0 + 2 || x >= first.cell.x1 - 2 || y < first.cell.y0 + 2 ||
                                y >= first.cell.y1 - 2;
            if (border) {
                EXPECT_EQ(pixel(x, y), 0);
            } else {
                ink += pixel(x, y) > 0 ? 1 : 0;
            }
        }
    }
    EXPECT_GT(ink, 20);

    // Fill a 64 px page with 40 px glyphs: new pages open, and no two cells on a page overlap.
    std::vector<AtlasGlyph> cells{first};
    const std::uint64_t before = atlas.version();
    for (char c = 'a'; c <= 'z'; ++c) {
        const auto& m = face.glyph_metrics(face.glyph_index(static_cast<unsigned char>(c)), isize);
        cells.push_back(atlas.glyph(face, 0, m, isize));
    }
    EXPECT_GT(atlas.page_count(), 2u);
    EXPECT_EQ(atlas.version(), before + 26u);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            if (cells[i].page == cells[j].page) {
                EXPECT_FALSE(overlaps(cells[i].cell, cells[j].cell)) << i << " " << j;
            }
        }
    }
    // The newest page's dirty rect since `before` covers what was written there.
    const AtlasPage& last = atlas.page(atlas.page_count() - 1);
    EXPECT_EQ(last.version(), atlas.version());
    const AtlasRect dirty = last.dirty_since(before);
    EXPECT_FALSE(dirty.empty());
    EXPECT_EQ(dirty.x0, 0);
    EXPECT_EQ(dirty.y0, 0);
    EXPECT_TRUE(last.dirty_since(atlas.version()).empty());
}

TEST(UiTextRender, AtlasGivesOversizedGlyphsTheirOwnPage) {
    FontFace face = load_face();
    GlyphAtlas atlas(64);
    const int isize = 2000;  // 200 px
    const auto& m = face.glyph_metrics(face.glyph_index('W'), isize);
    const AtlasGlyph& cell = atlas.glyph(face, 0, m, isize);
    EXPECT_GE(atlas.page(cell.page).width(), m.bitmap_width() + 4);
    EXPECT_GE(atlas.page(cell.page).height(), m.bitmap_height() + 4);
}

TEST(UiTextRender, GlyphQuadsSitWhereFontstashPutsThem) {
    FontFace face = load_face();
    GlyphAtlas atlas;
    const int isize = 200;
    const glm::vec2 at{100.0f, 50.0f};
    const auto quads = layout(face, atlas, "Hi", 20.0f, at, UiAlign::Start, UiAlign::Start);
    ASSERT_EQ(quads.size(), 2u);
    const auto& h = face.glyph_metrics(face.glyph_index('H'), isize);
    const float ascent = face.ascender() * static_cast<float>(isize) / 10.0f;
    // Top alignment puts the baseline at y + ascent; the quad is the bitmap plus a pixel each side, snapped down.
    EXPECT_FLOAT_EQ(quads[0].min.x, std::floor(at.x + static_cast<float>(h.box_x0 - 1)));
    EXPECT_FLOAT_EQ(quads[0].min.y, std::floor(at.y + ascent + static_cast<float>(h.box_y0 - 1)));
    EXPECT_FLOAT_EQ(quads[0].max.x - quads[0].min.x, static_cast<float>(h.bitmap_width() + 2));
    EXPECT_FLOAT_EQ(quads[0].max.y - quads[0].min.y, static_cast<float>(h.bitmap_height() + 2));
    // The second glyph starts one rounded advance (plus kerning) later.
    const auto& i = face.glyph_metrics(face.glyph_index('i'), isize);
    const float pen = at.x + glyph_advance(face, 'H', isize) + kern_pixels(face, 'H', 'i', isize);
    EXPECT_FLOAT_EQ(quads[1].min.x, std::floor(pen + static_cast<float>(i.box_x0 - 1)));

    // Texels: inside [0, 1], and as many as the quad has pixels at raster scale 1.
    for (const GlyphQuad& q : quads) {
        const AtlasPage& page = atlas.page(q.page);
        EXPECT_GE(q.uv_min.x, 0.0f);
        EXPECT_LE(q.uv_max.x, 1.0f);
        EXPECT_NEAR((q.uv_max.x - q.uv_min.x) * static_cast<float>(page.width()), q.max.x - q.min.x, 1e-3f);
        EXPECT_NEAR((q.uv_max.y - q.uv_min.y) * static_cast<float>(page.height()), q.max.y - q.min.y, 1e-3f);
    }

    // A space advances the pen but draws nothing.
    EXPECT_EQ(layout(face, atlas, "H i", 20.0f, at, UiAlign::Start, UiAlign::Start).size(), 2u);
    EXPECT_TRUE(layout(face, atlas, "   ", 20.0f, at, UiAlign::Start, UiAlign::Start).empty());
    EXPECT_TRUE(layout(face, atlas, "", 20.0f, at, UiAlign::Start, UiAlign::Start).empty());
}

TEST(UiTextRender, GlyphQuadsFollowEachAlignment) {
    FontFace face = load_face();
    GlyphAtlas atlas;
    const int isize = 180;
    const float size = 18.0f;
    const std::string_view text = "Align me";
    const glm::vec2 at{200.0f, 80.0f};
    const float advance = engine::ui::text_extent(face, text, size).x;
    const auto base = layout(face, atlas, text, size, at, UiAlign::Start, UiAlign::Start);
    ASSERT_FALSE(base.empty());

    // Horizontal: End moves the pen back by the whole advance, Center by half (then snapped per glyph).
    const auto end = layout(face, atlas, text, size, at, UiAlign::End, UiAlign::Start);
    const auto center = layout(face, atlas, text, size, at, UiAlign::Center, UiAlign::Start);
    ASSERT_EQ(end.size(), base.size());
    ASSERT_EQ(center.size(), base.size());
    for (std::size_t k = 0; k < base.size(); ++k) {
        EXPECT_FLOAT_EQ(base[k].min.x - end[k].min.x, advance);
        EXPECT_FLOAT_EQ(center[k].min.y, base[k].min.y);
        EXPECT_NEAR(base[k].min.x - center[k].min.x, advance * 0.5f, 1.0f);
    }

    // Vertical: Start = top (baseline at y + ascent), Center = middle of ascent and descent, End = bottom (baseline
    // at y + descent, which is negative).
    const float ascender = face.ascender() * static_cast<float>(isize) / 10.0f;
    const float descender = face.descender() * static_cast<float>(isize) / 10.0f;
    const float middle = (face.ascender() + face.descender()) / 2.0f * static_cast<float>(isize) / 10.0f;
    const auto& a = face.glyph_metrics(face.glyph_index('A'), isize);
    const auto top_y = [&](float baseline) { return std::floor(at.y + baseline + static_cast<float>(a.box_y0 - 1)); };
    EXPECT_FLOAT_EQ(base[0].min.y, top_y(ascender));
    EXPECT_FLOAT_EQ(layout(face, atlas, text, size, at, UiAlign::Start, UiAlign::Center)[0].min.y, top_y(middle));
    EXPECT_FLOAT_EQ(layout(face, atlas, text, size, at, UiAlign::Start, UiAlign::End)[0].min.y, top_y(descender));
    // Anything else is Start, as fill_text's `default:` was.
    EXPECT_FLOAT_EQ(layout(face, atlas, text, size, at, UiAlign::SpaceBetween, UiAlign::SpaceBetween)[0].min.x,
            base[0].min.x);
}

TEST(UiTextRender, RasterScaleRasterizesLargerAndMapsBack) {
    FontFace face = load_face();
    GlyphAtlas atlas;
    const auto quads = layout(face, atlas, "M", 16.0f, {10.0f, 10.0f}, UiAlign::Start, UiAlign::Start, 2.0f);
    ASSERT_EQ(quads.size(), 1u);
    const auto& m = face.glyph_metrics(face.glyph_index('M'), 320);
    // Rasterized at 32 px, drawn at half that.
    EXPECT_FLOAT_EQ(quads[0].max.x - quads[0].min.x, static_cast<float>(m.bitmap_width() + 2) / 2.0f);
    const AtlasPage& page = atlas.page(quads[0].page);
    EXPECT_NEAR((quads[0].uv_max.x - quads[0].uv_min.x) * static_cast<float>(page.width()),
            static_cast<float>(m.bitmap_width() + 2), 1e-3f);
    // A scale that quantizes the size below 0.2 px draws nothing.
    EXPECT_TRUE(layout(face, atlas, "M", 16.0f, {}, UiAlign::Start, UiAlign::Start, 0.0f).empty());
}

TEST(UiTextRender, LibraryResolvesFontsAndKeepsTheMathFontApart) {
    FontLibrary library;
    const AssetId other{"0123456789abcdef0123456789abcdef"};
    // No face yet: the NanoVG painter's no-context answers.
    EXPECT_EQ(library.face(engine::builtin::font_ui), nullptr);
    EXPECT_EQ(library.measure_text("abcd", engine::builtin::font_ui, 10.0f), glm::vec2(20.0f, 10.0f));
    EXPECT_FLOAT_EQ(library.font_metrics(other, 10.0f).ascent, 8.0f);
    EXPECT_EQ(library.break_lines("ab cd", other, 10.0f, 12.0f).lines.size(), 2u);

    EXPECT_FALSE(library.add_font(other, Font{}));
    ASSERT_TRUE(library.add_font(engine::builtin::font_ui, ui_font()));
    EXPECT_TRUE(library.add_font(engine::builtin::font_ui, ui_font()));
    EXPECT_TRUE(library.has_font(engine::builtin::font_ui));
    EXPECT_FALSE(library.has_font(other));
    // An unknown font is the UI font.
    EXPECT_EQ(library.face(other), library.face(engine::builtin::font_ui));
    EXPECT_EQ(library.measure_text("Fallback", other, 14.0f), library.measure_text("Fallback", AssetId{}, 14.0f));

    // The math font is a MathFont, never a text face.
    EXPECT_EQ(library.math_font(), nullptr);
    ASSERT_TRUE(library.add_font(engine::builtin::font_math, read_builtin("fonts/math.otf")));
    ASSERT_NE(library.math_font(), nullptr);
    EXPECT_FALSE(library.has_font(engine::builtin::font_math));
    EXPECT_EQ(library.face(engine::builtin::font_math), library.face(engine::builtin::font_ui));

    std::vector<GlyphQuad> quads;
    const float end_x = library.layout_text(
            "Text", other, 16.0f, {0.0f, 0.0f}, UiAlign::Start, UiAlign::Start, 1.0f, quads);
    EXPECT_EQ(quads.size(), 4u);
    EXPECT_FLOAT_EQ(end_x, library.measure_text("Text", other, 16.0f).x);
    EXPECT_GE(library.atlas().glyph_count(), 4u);
}

// Numbers the fontstash parity test (removed with NanoVG in wind-234 5e) proved equal to NanoVG's, kept as literals so
// a change to the text path shows up as a changed width, metric, quad, or glyph bitmap.
TEST(UiTextRender, KeepsTheNumbersNanoVgProduced) {
    FontFace face = load_face();
    struct Metrics {
        float size, ascent, descent, line_height;
    };
    const Metrics metrics[] = {
            {11.0f, 8.80710316f, 2.19289756f, 11.0f},
            {13.37f, 10.6485882f, 2.65141249f, 13.3000002f},
            {16.0f, 12.8103313f, 3.18966913f, 16.0f},
            {24.0f, 19.2154961f, 4.78450346f, 24.0f},
    };
    for (const Metrics& m : metrics) {
        const auto ours = engine::ui::text_metrics(face, m.size);
        EXPECT_FLOAT_EQ(ours.ascent, m.ascent) << m.size;
        EXPECT_FLOAT_EQ(ours.descent, m.descent) << m.size;
        EXPECT_FLOAT_EQ(ours.line_height, m.line_height) << m.size;
    }

    struct Extent {
        const char* text;
        float at_13_37, at_16;
    };
    const Extent extents[] = {
            {"Hello, world", 75.0f, 92.0f},
            {"AVATAR To Ty Wo Yo P. F, L' VA", 211.0f, 255.0f},
            {"$7.50, 1.1 7, 77. 4,5", 121.0f, 145.0f},
            {"caf\xC3\xA9 \xE2\x82\xAC \xE2\x80\x94", 58.0f, 70.0f},
    };
    for (const Extent& e : extents) {
        EXPECT_EQ(engine::ui::text_extent(face, e.text, 13.37f), glm::vec2(e.at_13_37, 13.3000002f)) << e.text;
        EXPECT_EQ(engine::ui::text_extent(face, e.text, 16.0f), glm::vec2(e.at_16, 16.0f)) << e.text;
    }

    // "Hello" at 16 px under a 1.5 scale: each quad, and the sum of its glyph's texels.
    struct Quad {
        glm::vec2 min, max;
        long ink;
    };
    const Quad expected[] = {
            {{37.3333359f, 19.333334f}, {48.0f, 32.6666679f}, 24433},
            {{48.6666679f, 22.0f}, {58.0f, 33.3333359f}, 19084},
            {{58.0f, 19.333334f}, {61.3333359f, 32.6666679f}, 9394},
            {{62.0f, 19.333334f}, {65.3333359f, 32.6666679f}, 9394},
            {{66.0f, 22.0f}, {76.0f, 33.3333359f}, 16988},
    };
    GlyphAtlas atlas;
    const float scale = 1.5f;
    const auto quads = layout(face, atlas, "Hello", 16.0f, {37.25f, 19.5f}, UiAlign::Start, UiAlign::Start, scale);
    ASSERT_EQ(quads.size(), std::size(expected));
    for (std::size_t k = 0; k < quads.size(); ++k) {
        const GlyphQuad& q = quads[k];
        EXPECT_FLOAT_EQ(q.min.x, expected[k].min.x) << k;
        EXPECT_FLOAT_EQ(q.min.y, expected[k].min.y) << k;
        EXPECT_FLOAT_EQ(q.max.x, expected[k].max.x) << k;
        EXPECT_FLOAT_EQ(q.max.y, expected[k].max.y) << k;
        const AtlasPage& page = atlas.page(q.page);
        const int ox = static_cast<int>(std::lround(q.uv_min.x * static_cast<float>(page.width())));
        const int oy = static_cast<int>(std::lround(q.uv_min.y * static_cast<float>(page.height())));
        const int w = static_cast<int>(std::lround((q.max.x - q.min.x) * scale));
        const int h = static_cast<int>(std::lround((q.max.y - q.min.y) * scale));
        long ink = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                ink += page.pixels()[static_cast<std::size_t>(oy + y) * static_cast<std::size_t>(page.width()) +
                                     static_cast<std::size_t>(ox + x)];
            }
        }
        EXPECT_EQ(ink, expected[k].ink) << k;
    }
}
