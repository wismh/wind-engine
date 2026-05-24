#include <gtest/gtest.h>

#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return sheet.value_or(engine::ui::Stylesheet{});
}

void set_window(engine::ecs::World &world, int width, int height) {
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{width, height};
}

// Lays the canvas out the way a pointer event does, so `@media` sees `media_size` of that canvas.
float title_width(engine::ui::UiFit fit, engine::render::Rect rect, glm::vec2 reference, int window_w, int window_h,
                  std::string_view css, float click_x, float click_y) {
    engine::ecs::World world;
    set_window(world, window_w, window_h);
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label id="title" text="Hi"/></Canvas>)");
    EXPECT_TRUE(parsed.has_value());
    if (!parsed) {
        return 0.0f;
    }
    engine::ui::UiCanvas canvas;
    canvas.rect = rect;
    canvas.fit = fit;
    canvas.reference_size = reference;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, must_parse_css(css)});
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, click_x, click_y);
    engine::ui::Element *title =
            engine::ui::find_by_id(world.get<engine::ui::UiInstance>(entity).document.root, "title");
    EXPECT_NE(title, nullptr);
    return title != nullptr ? title->layout_rect.w : 0.0f;
}

constexpr char kQuery[] = R"(
    #title { width: 40; height: 20; }
    @media (min-width: 400) { #title { width: 200; } }
)";

} // namespace

TEST(UiMedia, FixedCanvasUsesItsOwnWidth) {
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::Fixed, {0.0f, 0.0f, 300.0f, 200.0f}, {}, 1200, 800, kQuery, 10.0f,
                                10.0f),
                    40.0f);
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::Fixed, {0.0f, 0.0f, 500.0f, 200.0f}, {}, 1200, 800, kQuery, 10.0f,
                                10.0f),
                    200.0f);
}

TEST(UiMedia, FillWindowUsesTheWindow) {
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::FillWindow, {}, {}, 1200, 800, kQuery, 10.0f, 10.0f), 200.0f);
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::FillWindow, {}, {}, 300, 800, kQuery, 10.0f, 10.0f), 40.0f);
}

TEST(UiMedia, ScaledCanvasUsesTheDesignBox) {
    constexpr char kWideWindow[] = R"(
        #title { width: 40; height: 20; }
        @media (min-width: 900) { #title { width: 200; } }
    )";
    constexpr char kDesign[] = R"(
        #title { width: 40; height: 20; }
        @media (min-width: 700) { #title { width: 200; } }
    )";
    // Window is 2000px. The design box is 800, so 900 does not match and 700 does.
    // The click sits inside the letterboxed canvas rect.
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::ScaleWithScreenSize, {}, {800.0f, 600.0f}, 2000, 2000, kWideWindow,
                                100.0f, 400.0f),
                    40.0f);
    EXPECT_FLOAT_EQ(title_width(engine::ui::UiFit::ScaleWithScreenSize, {}, {800.0f, 600.0f}, 2000, 2000, kDesign,
                                100.0f, 400.0f),
                    200.0f);
}
