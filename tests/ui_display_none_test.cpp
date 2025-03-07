#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using engine::ui::Element;
using engine::ui::UiDocument;

Element* find_class(Element& root, std::string_view class_name) {
    if (std::ranges::find(root.classes, class_name) != root.classes.end()) {
        return &root;
    }
    for (Element& child : root.children) {
        if (Element* found = find_class(child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

UiDocument laid_out(std::string_view xml, std::string_view css) {
    auto parsed = engine::ui::parse_xml(xml);
    EXPECT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    engine::ui::apply_layout_style(parsed->root, &*sheet, 400.0f, 400.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    return std::move(*parsed);
}

constexpr std::string_view kColumnXml =
        R"(<Canvas><Stack class="col"><Stack class="a"/><Stack class="b"/><Stack class="c"/></Stack></Canvas>)";
constexpr std::string_view kColumnCss =
        ".col { gap: 10; } .a { width: 50; height: 20; } .b { width: 50; height: 30; } .c { width: 50; height: 40; }";

TEST(UiDisplayNone, ParsesIntoTheElement) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { display: none; }");
    EXPECT_FALSE(find_class(doc.root, "a")->display_none);
    EXPECT_TRUE(find_class(doc.root, "b")->display_none);
}

TEST(UiDisplayNone, ValuesOtherThanNoneStayDisplayed) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { display: flex; }");
    EXPECT_FALSE(find_class(doc.root, "b")->display_none);
}

TEST(UiDisplayNone, HiddenSiblingTakesNoSpaceAndNoGap) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { display: none; }");
    EXPECT_FLOAT_EQ(find_class(doc.root, "a")->layout_rect.y, 0.0f);
    // a (20) + one gap (10); b contributes neither its height nor a second gap.
    EXPECT_FLOAT_EQ(find_class(doc.root, "c")->layout_rect.y, 30.0f);
}

TEST(UiDisplayNone, VisibilityHiddenStillTakesSpace) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { visibility: hidden; }");
    EXPECT_FLOAT_EQ(find_class(doc.root, "c")->layout_rect.y, 20.0f + 10.0f + 30.0f + 10.0f);
}

TEST(UiDisplayNone, HiddenElementHasNoGeometry) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { display: none; }");
    const engine::render::Rect rect = find_class(doc.root, "b")->layout_rect;
    EXPECT_FLOAT_EQ(rect.w, 0.0f);
    EXPECT_FLOAT_EQ(rect.h, 0.0f);
}

TEST(UiDisplayNone, HugParentShrinksByTheHiddenChild) {
    auto doc = laid_out(kColumnXml, std::string(kColumnCss) + " .b { display: none; }");
    // a (20) + gap (10) + c (40).
    EXPECT_FLOAT_EQ(find_class(doc.root, "col")->layout_rect.h, 70.0f);
}

TEST(UiDisplayNone, HiddenAbsoluteChildIsNotLaidOut) {
    auto doc = laid_out(R"(<Canvas><Stack class="col"><Stack class="p"/></Stack></Canvas>)",
            ".p { position: absolute; left: 5; top: 5; width: 10; height: 10; display: none; }");
    EXPECT_FLOAT_EQ(find_class(doc.root, "p")->layout_rect.w, 0.0f);
}

TEST(UiDisplayNone, ReHidingClearsStaleGeometry) {
    auto parsed = engine::ui::parse_xml(kColumnXml);
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const auto shown = engine::ui::parse_css(kColumnCss, warnings);
    const auto hidden = engine::ui::parse_css(std::string(kColumnCss) + " .b { display: none; }", warnings);
    ASSERT_TRUE(shown.has_value());
    ASSERT_TRUE(hidden.has_value());
    const engine::render::Rect canvas{0.0f, 0.0f, 400.0f, 400.0f};

    engine::ui::apply_layout_style(parsed->root, &*shown, 400.0f, 400.0f);
    engine::ui::layout(*parsed, canvas);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "b")->layout_rect.h, 30.0f);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 70.0f);

    engine::ui::apply_layout_style(parsed->root, &*hidden, 400.0f, 400.0f);
    engine::ui::layout(*parsed, canvas);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "b")->layout_rect.h, 0.0f);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 30.0f);

    engine::ui::apply_layout_style(parsed->root, &*shown, 400.0f, 400.0f);
    engine::ui::layout(*parsed, canvas);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 70.0f);
}

constexpr std::string_view kButtonXml =
        R"(<Canvas><Stack class="col"><Stack class="wrap"><Button class="btn"/></Stack></Stack></Canvas>)";
constexpr std::string_view kButtonCss = ".btn { width: 100; height: 40; }";

TEST(UiDisplayNone, HitTestFindsTheButtonWhenDisplayed) {
    auto doc = laid_out(kButtonXml, kButtonCss);
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 10.0f), find_class(doc.root, "btn"));
}

TEST(UiDisplayNone, HitTestSkipsAHiddenButton) {
    auto doc = laid_out(kButtonXml, std::string(kButtonCss) + " .btn { display: none; }");
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 10.0f), nullptr);
}

TEST(UiDisplayNone, HitTestSkipsTheDescendantsOfAHiddenParent) {
    auto doc = laid_out(kButtonXml, std::string(kButtonCss) + " .wrap { display: none; }");
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 10.0f), nullptr);
}

TEST(UiDisplayNone, HitTestSkipsAVisibilityHiddenButton) {
    auto doc = laid_out(kButtonXml, std::string(kButtonCss) + " .btn { visibility: hidden; }");
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 10.0f), nullptr);
}

TEST(UiDisplayNone, HiddenButtonDoesNotBlockTheOneBehindIt) {
    auto doc = laid_out(
            R"(<Canvas><Stack class="col"><Button class="back"/><Button class="front"/></Stack></Canvas>)",
            ".back { width: 100; height: 40; } "
            ".front { position: absolute; left: 0; top: 0; width: 100; height: 40; z-index: 5; display: none; }");
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 10.0f), find_class(doc.root, "back"));
}

}
