#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string_view>

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "ui layout hit tests must not include nvg headers"
#endif

namespace {

class ClickViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::RelayCommand click;

    ClickViewModel() {
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
    }
};

// Height 8 instead of font-size (fallback uses size as height) so a hug Label shifts later
// siblings by a visible amount between paint and a painter-less hit-test.
class ShortTextPainter final : public engine::ui::IUiPainter {
public:
    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, 8.0f};
    }
};

}

TEST(UiLayoutHit, PointerLayoutMatchesPaintWhenPainterRegistered) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Stack><Label text="Title"/><Button command="{binding click}" content="Go"/></Stack></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 100.0f, 100.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    ShortTextPainter painter;
    engine::ui::paint_document(instance.document, nullptr, painter, engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    const engine::ui::Element* painted_button =
            engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::Button);
    ASSERT_NE(painted_button, nullptr);
    const engine::render::Rect paint_rect = painted_button->layout_rect;
    ASSERT_GT(paint_rect.h, 0.0f);
    const float click_x = paint_rect.x + paint_rect.w * 0.5f;
    const float click_y = paint_rect.y + paint_rect.h * 0.5f;

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, click_x, click_y);
    EXPECT_EQ(vm->clicks, 0);

    world.ctx<engine::ui::UiLayoutPainters>().resolve = [&painter](engine::WindowId) -> engine::ui::IUiPainter* {
        return &painter;
    };
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, click_x, click_y);
    EXPECT_EQ(vm->clicks, 1);

    const engine::ui::Element* hit_button =
            engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::Button);
    ASSERT_NE(hit_button, nullptr);
    EXPECT_FLOAT_EQ(hit_button->layout_rect.x, paint_rect.x);
    EXPECT_FLOAT_EQ(hit_button->layout_rect.y, paint_rect.y);
    EXPECT_FLOAT_EQ(hit_button->layout_rect.w, paint_rect.w);
    EXPECT_FLOAT_EQ(hit_button->layout_rect.h, paint_rect.h);
}
