#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// CSS `pointer-events`: a panel box can take the pointer without a command. docs/tech/features/UI Input.md.

namespace {

using engine::ui::Element;
using engine::ui::PointerEvents;
using engine::ui::UiDocument;

constexpr engine::render::Rect kCanvas{0.0f, 0.0f, 400.0f, 400.0f};

constexpr std::string_view kPanel = R"(<Canvas>
  <Stack class="panel">
    <Label class="wave" text="3"/>
    <Button class="go" content="B"/>
  </Stack>
</Canvas>)";

constexpr std::string_view kBox =
        ".panel { position: absolute; left: 0; top: 0; width: 200px; height: 80px; } "
        ".wave { position: absolute; left: 80px; top: 10px; width: 40px; height: 20px; } "
        ".go { position: absolute; left: 0; top: 0; width: 40px; height: 24px; } ";

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

engine::ui::Stylesheet sheet_of(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? "" : warnings[0]);
    return sheet.value_or(engine::ui::Stylesheet{});
}

UiDocument laid_out(std::string_view css) {
    auto parsed = engine::ui::parse_xml(kPanel);
    EXPECT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = sheet_of(css);
    engine::ui::apply_layout_style(parsed->root, &sheet, kCanvas.w, kCanvas.h);
    engine::ui::layout(*parsed, kCanvas);
    return std::move(*parsed);
}

} // namespace

TEST(UiPointerEvents, ParsesAllNoneAndAnUnknownValueStaysAuto) {
    const std::pair<std::string_view, PointerEvents> cases[] = {
            {"auto", PointerEvents::Auto},
            {"all", PointerEvents::All},
            {"none", PointerEvents::None},
            {"bounding-box", PointerEvents::Auto},
    };
    for (const auto& [value, expected] : cases) {
        UiDocument doc = laid_out(std::string(kBox) + ".panel { pointer-events: " + std::string(value) + "; }");
        const Element* panel = find_class(doc.root, "panel");
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(panel->pointer_events, expected) << value;
    }
}

TEST(UiPointerEvents, AllBlocksThePanelAndAChildButtonStillWins) {
    UiDocument doc = laid_out(std::string(kBox) + ".panel { pointer-events: all; }");

    Element* button = engine::ui::hit_test(doc.root, 10.0f, 10.0f);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->kind, engine::ui::ElementKind::Button);

    Element* wave = engine::ui::hit_test(doc.root, 90.0f, 20.0f);
    ASSERT_NE(wave, nullptr);
    EXPECT_EQ(find_class(*wave, "panel"), wave);

    Element* background = engine::ui::hit_test(doc.root, 10.0f, 60.0f);
    ASSERT_NE(background, nullptr);
    EXPECT_EQ(find_class(*background, "panel"), background);

    EXPECT_EQ(engine::ui::hit_test(doc.root, 250.0f, 10.0f), nullptr);
}

TEST(UiPointerEvents, AutoLetsThePanelBackgroundThrough) {
    UiDocument doc = laid_out(kBox);

    Element* button = engine::ui::hit_test(doc.root, 10.0f, 10.0f);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->kind, engine::ui::ElementKind::Button);
    EXPECT_EQ(engine::ui::hit_test(doc.root, 90.0f, 20.0f), nullptr);
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 60.0f), nullptr);
}

TEST(UiPointerEvents, NoneOnThePanelKeepsTheButtonAndDropsTheBackground) {
    UiDocument doc = laid_out(std::string(kBox) + ".panel { pointer-events: none; }");

    Element* button = engine::ui::hit_test(doc.root, 10.0f, 10.0f);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->kind, engine::ui::ElementKind::Button);
    EXPECT_EQ(engine::ui::hit_test(doc.root, 90.0f, 20.0f), nullptr);
    EXPECT_EQ(engine::ui::hit_test(doc.root, 10.0f, 60.0f), nullptr);
}

TEST(UiPointerEvents, NoneOnAButtonLetsTheAllPanelCatchThatPoint) {
    UiDocument doc = laid_out(std::string(kBox) + ".panel { pointer-events: all; } .go { pointer-events: none; }");

    Element* hit = engine::ui::hit_test(doc.root, 10.0f, 10.0f);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(find_class(*hit, "panel"), hit);
}

TEST(UiPointerEvents, HoverRuleDoesNotChangeIt) {
    auto parsed = engine::ui::parse_xml(kPanel);
    ASSERT_TRUE(parsed.has_value());
    Element* panel = find_class(parsed->root, "panel");
    ASSERT_NE(panel, nullptr);
    panel->hovered = true;
    const std::string css = std::string(kBox) + ".panel:hover { pointer-events: all; }";
    const engine::ui::Stylesheet sheet = sheet_of(css);
    engine::ui::apply_layout_style(parsed->root, &sheet, kCanvas.w, kCanvas.h);
    engine::ui::layout(*parsed, kCanvas);

    panel = find_class(parsed->root, "panel");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->pointer_events, PointerEvents::Auto);
    EXPECT_EQ(engine::ui::hit_test(parsed->root, 10.0f, 60.0f), nullptr);
}

TEST(UiPointerEvents, CollectsTheAllBoxAndSkipsNone) {
    UiDocument all = laid_out(std::string(kBox) + ".panel { pointer-events: all; }");
    std::vector<engine::render::Rect> rects;
    engine::ui::collect_interactive_rects(all.root, rects);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_EQ(rects[0], find_class(all.root, "panel")->layout_rect);

    UiDocument plain = laid_out(kBox);
    rects.clear();
    engine::ui::collect_interactive_rects(plain.root, rects);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_EQ(rects[0], find_class(plain.root, "go")->layout_rect);

    UiDocument dropped = laid_out(std::string(kBox) + ".panel { pointer-events: none; }");
    rects.clear();
    engine::ui::collect_interactive_rects(dropped.root, rects);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_EQ(rects[0], find_class(dropped.root, "go")->layout_rect);
}

TEST(UiPointerEvents, NoneDoesNotClaimTheCursor) {
    UiDocument doc = laid_out(std::string(kBox) +
                              ".panel { pointer-events: none; cursor: pointer; } .go { cursor: move; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), engine::ui::Cursor::Move);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 60.0f), engine::ui::Cursor::Default);
}
