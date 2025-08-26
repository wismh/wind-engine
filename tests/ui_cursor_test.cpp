#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/cursor.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CSS `cursor`: cursor_at() over one tree, update_cursor() over a window's canvases. docs/tech/features/UI Input.md.

namespace {

using engine::ui::Cursor;
using engine::ui::Element;
using engine::ui::UiDocument;

constexpr engine::render::Rect kCanvas{0.0f, 0.0f, 400.0f, 400.0f};

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

UiDocument laid_out(std::string_view xml, std::string_view css) {
    auto parsed = engine::ui::parse_xml(xml);
    EXPECT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = sheet_of(css);
    engine::ui::apply_layout_style(parsed->root, &sheet, kCanvas.w, kCanvas.h);
    engine::ui::layout(*parsed, kCanvas);
    return std::move(*parsed);
}

constexpr std::string_view kNested = R"(<Canvas><Stack class="outer"><Stack class="inner"/></Stack></Canvas>)";
constexpr std::string_view kNestedCss =
        ".outer { position: absolute; left: 0; top: 0; width: 200; height: 200; } "
        ".inner { position: absolute; left: 50; top: 50; width: 50; height: 50; } ";

}

TEST(UiCursor, ParsesEveryKeywordAndAnUnknownOneStaysAuto) {
    const std::pair<std::string_view, Cursor> cases[] = {
            {"auto", Cursor::Auto},
            {"default", Cursor::Default},
            {"pointer", Cursor::Pointer},
            {"text", Cursor::Text},
            {"crosshair", Cursor::Crosshair},
            {"wait", Cursor::Wait},
            {"progress", Cursor::Progress},
            {"move", Cursor::Move},
            {"not-allowed", Cursor::NotAllowed},
            {"ew-resize", Cursor::EwResize},
            {"ns-resize", Cursor::NsResize},
            {"nwse-resize", Cursor::NwseResize},
            {"nesw-resize", Cursor::NeswResize},
            {"grab", Cursor::Auto},
    };
    for (const auto& [value, expected] : cases) {
        UiDocument doc = laid_out(kNested, std::string(kNestedCss) + ".inner { cursor: " + std::string(value) + "; }");
        EXPECT_EQ(find_class(doc.root, "inner")->cursor, expected) << value;
    }
}

TEST(UiCursor, AutoTakesTheNearestAncestorsCursor) {
    UiDocument doc = laid_out(kNested, std::string(kNestedCss) + ".outer { cursor: move; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 60.0f, 60.0f), Cursor::Move);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), Cursor::Move);
    // Only the canvas is there, and it sets none.
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 300.0f, 300.0f), Cursor::Default);
    // Outside the canvas.
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 500.0f, 500.0f), Cursor::Default);

    doc = laid_out(kNested, std::string(kNestedCss) + ".outer { cursor: move; } .inner { cursor: pointer; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 60.0f, 60.0f), Cursor::Pointer);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), Cursor::Move);
}

TEST(UiCursor, TextInputAndSelectableLabelShowTextUnlessTheyNameACursor) {
    constexpr std::string_view xml = R"(<Canvas><Stack class="row">)"
                                     R"(<TextInput class="t"/><Label class="l" text="hi"/><Button class="b"/>)"
                                     R"(</Stack></Canvas>)";
    const std::string css = ".row { position: absolute; left: 0; top: 0; width: 300; height: 100; cursor: pointer; } "
                            ".t { position: absolute; left: 0; top: 0; width: 100; height: 20; } "
                            ".l { position: absolute; left: 100; top: 0; width: 100; height: 20; user-select: text; } "
                            ".b { position: absolute; left: 200; top: 0; width: 100; height: 20; } ";
    UiDocument doc = laid_out(xml, css);
    // `auto` over text is Text, even under an ancestor that sets a cursor.
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), Cursor::Text);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 110.0f, 10.0f), Cursor::Text);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 210.0f, 10.0f), Cursor::Pointer);

    doc = laid_out(xml, css + ".t { cursor: not-allowed; } .l { user-select: none; } .b { cursor: default; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), Cursor::NotAllowed);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 110.0f, 10.0f), Cursor::Pointer);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 210.0f, 10.0f), Cursor::Default);
}

TEST(UiCursor, TheTopmostElementDecidesAndAHiddenOneIsSkipped) {
    constexpr std::string_view xml = R"(<Canvas><Stack class="front"/><Stack class="back"/></Canvas>)";
    const std::string css =
            ".front { position: absolute; left: 0; top: 0; width: 100; height: 100; z-index: 1; cursor: pointer; } "
            ".back { position: absolute; left: 50; top: 50; width: 100; height: 100; cursor: move; } ";
    UiDocument doc = laid_out(xml, css);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 75.0f, 75.0f), Cursor::Pointer);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 125.0f, 125.0f), Cursor::Move);

    // An `auto` front element is still the element there: it takes its ancestors' cursor, not the one behind it.
    doc = laid_out(xml, css + ".front { cursor: auto; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 75.0f, 75.0f), Cursor::Default);
    doc = laid_out(xml, css + ".front { display: none; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 75.0f, 75.0f), Cursor::Move);
    doc = laid_out(xml, css + ".front { visibility: hidden; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 75.0f, 75.0f), Cursor::Move);
}

TEST(UiCursor, AnAbsoluteChildOutsideAParentThatDoesNotClipKeepsItsCursor) {
    constexpr std::string_view xml = R"(<Canvas><Stack class="p"><Stack class="c"/></Stack></Canvas>)";
    const std::string css = ".p { position: absolute; left: 0; top: 0; width: 20; height: 20; cursor: move; } "
                            ".c { position: absolute; left: 100; top: 100; width: 20; height: 20; } ";
    UiDocument doc = laid_out(xml, css);
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 110.0f, 110.0f), Cursor::Move);
    doc = laid_out(xml, css + ".c { cursor: ew-resize; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 110.0f, 110.0f), Cursor::EwResize);
    // A clipping parent neither paints it there nor puts its cursor there.
    doc = laid_out(xml, css + ".c { cursor: ew-resize; } .p { overflow: hidden; }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 110.0f, 110.0f), Cursor::Default);
}

TEST(UiCursor, ACustomPropertyCanNameTheCursor) {
    UiDocument doc = laid_out(R"(<Canvas><Stack class="c"/></Canvas>)",
            ".c { position: absolute; left: 0; top: 0; width: 20; height: 20; --resize: ns-resize; "
            "cursor: var(--resize, ew-resize); }");
    EXPECT_EQ(engine::ui::cursor_at(doc.root, 10.0f, 10.0f), Cursor::NsResize);
}

namespace {

struct WindowFixture {
    engine::ecs::World world;

    void add_canvas(int order, engine::render::Rect rect, std::string_view xml, std::string_view css) {
        engine::ui::UiCanvas canvas;
        canvas.rect = rect;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.order = order;
        auto parsed = engine::ui::parse_xml(xml);
        EXPECT_TRUE(parsed.has_value());
        engine::ui::Stylesheet sheet = sheet_of(css);
        engine::ui::apply_layout_style(parsed->root, &sheet, kCanvas.w, kCanvas.h);
        engine::ui::layout(*parsed, rect);
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed), std::move(sheet)});
    }

    Cursor at(float x, float y, engine::WindowId window = engine::kPrimaryWindow) {
        engine::ui::pointer_for(world, window).position = {x, y};
        return engine::ui::update_cursor(world, window);
    }
};

}

TEST(UiCursor, TheTopmostCanvasUnderThePointerDecides) {
    WindowFixture fx;
    fx.add_canvas(0, kCanvas, R"(<Canvas><Stack class="all"/></Canvas>)",
            ".all { position: absolute; left: 0; top: 0; width: 400; height: 400; cursor: move; }");
    // Above it in the corner, setting no cursor: the canvas a click there goes to.
    fx.add_canvas(1, engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f}, "<Canvas/>", "");
    EXPECT_EQ(fx.at(50.0f, 50.0f), Cursor::Default);
    EXPECT_EQ(fx.at(500.0f, 500.0f), Cursor::Default);
    EXPECT_EQ(fx.at(200.0f, 200.0f), Cursor::Move);
    // Canvases of another window are not under that window's pointer.
    EXPECT_EQ(fx.at(200.0f, 200.0f, engine::WindowId{7}), Cursor::Default);
    EXPECT_EQ(engine::ui::presentation_of(fx.world).cursors.cursors.at(engine::kPrimaryWindow), Cursor::Move);
}

TEST(UiCursor, APressKeepsTheCursorItStartedWithUntilRelease) {
    WindowFixture fx;
    fx.add_canvas(0, kCanvas, R"(<Canvas><Stack class="bar"/></Canvas>)",
            ".bar { position: absolute; left: 100; top: 0; width: 4; height: 400; cursor: ew-resize; }");
    engine::ui::UiPointer& pointer = engine::ui::pointer_for(fx.world, engine::kPrimaryWindow);
    EXPECT_EQ(fx.at(101.0f, 50.0f), Cursor::EwResize);
    pointer.down = true;
    // The drag outruns the 4px bar.
    EXPECT_EQ(fx.at(160.0f, 50.0f), Cursor::EwResize);
    pointer.down = false;
    EXPECT_EQ(fx.at(160.0f, 50.0f), Cursor::Default);

    // A press that starts off the bar keeps Default across it.
    pointer.down = true;
    EXPECT_EQ(fx.at(101.0f, 50.0f), Cursor::Default);
}
