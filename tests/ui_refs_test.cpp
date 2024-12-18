#include <gtest/gtest.h>

#include "ui/ui_refs.h"

#include <engine/builtin_ids.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using engine::AssetId;
using engine::ui::collect_referenced_fonts;
using engine::ui::collect_referenced_images;

constexpr std::string_view kImageId = "c1a1c2d3e4f5678901234567890abc09";
constexpr std::string_view kNestedImageId = "c1a1c2d3e4f5678901234567890abc0a";
constexpr std::string_view kCssImageId = "c1a1c2d3e4f5678901234567890abc0b";
constexpr std::string_view kFontId = "c1a1c2d3e4f5678901234567890abc07";
constexpr std::string_view kCssFontId = "c1a1c2d3e4f5678901234567890abc08";

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

engine::ui::UiDocument must_parse_xml(std::string_view xml) {
    auto parsed = engine::ui::parse_xml(xml, nullptr);
    EXPECT_TRUE(parsed.has_value());
    return *parsed;
}

bool contains(const std::vector<AssetId>& ids, std::string_view hex) {
    const auto id = AssetId::parse(hex);
    return id.has_value() && std::find(ids.begin(), ids.end(), *id) != ids.end();
}

}

TEST(UiRefs, CollectsElementSourceRecursively) {
    const auto document = must_parse_xml(R"(
<Canvas>
  <Image source=")" + std::string(kImageId) + R"("/>
  <Stack>
    <Image source=")" + std::string(kNestedImageId) + R"("/>
  </Stack>
</Canvas>
)");

    const auto images = collect_referenced_images(document, nullptr);
    EXPECT_TRUE(contains(images, kImageId));
    EXPECT_TRUE(contains(images, kNestedImageId));
    EXPECT_EQ(images.size(), 2u);
}

TEST(UiRefs, CollectsBackgroundImageFromStylesheetIgnoringNone) {
    const auto document = must_parse_xml("<Canvas><Button class=\"a\"/><Button class=\"b\"/></Canvas>");
    const auto sheet = must_parse_css(".a { background-image: " + std::string(kCssImageId) +
            "; } .b { background-image: none; }");

    const auto images = collect_referenced_images(document, &sheet);
    EXPECT_TRUE(contains(images, kCssImageId));
    EXPECT_EQ(images.size(), 1u);
}

TEST(UiRefs, NoDocumentOrStylesheetReferencesYieldsEmpty) {
    const auto document = must_parse_xml("<Canvas><Stack><Label text=\"hi\"/></Stack></Canvas>");
    EXPECT_TRUE(collect_referenced_images(document, nullptr).empty());
    EXPECT_TRUE(collect_referenced_fonts(document, nullptr).empty());
}

TEST(UiRefs, CollectsFontFamilyFromCssExcludingBuiltin) {
    const auto document = must_parse_xml("<Canvas><Label class=\"title\" text=\"hi\"/></Canvas>");
    const auto sheet = must_parse_css(".title { font-family: " + std::string(kCssFontId) + "; }");

    const auto fonts = collect_referenced_fonts(document, &sheet);
    EXPECT_TRUE(contains(fonts, kCssFontId));
    EXPECT_FALSE(contains(fonts, engine::builtin::font_ui.hex()));
    EXPECT_EQ(fonts.size(), 1u);
}

TEST(UiRefs, ElementFontFamilySetByPriorPaintPassIsCollected) {
    auto document = must_parse_xml("<Canvas><Label text=\"hi\"/></Canvas>");
    // Simulates paint.cpp writing a resolved (non-default) font-family back onto the element
    // after computing style (paint.cpp:605) — the collector must see it same as any other field.
    document.root.children.front().font_family = *AssetId::parse(kFontId);

    const auto fonts = collect_referenced_fonts(document, nullptr);
    EXPECT_TRUE(contains(fonts, kFontId));
}
