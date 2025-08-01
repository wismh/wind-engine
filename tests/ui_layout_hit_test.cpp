#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    void apply_view(glm::vec2, glm::vec2, float) override {}
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

class ViewportCameraViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::Bindable<float> pan_x;
    engine::ui::Bindable<float> pan_y;
    engine::ui::Bindable<float> zoom;
    engine::ui::RelayCommand click;

    ViewportCameraViewModel() {
        property(engine::ui::intern("pan_x"), pan_x);
        property(engine::ui::intern("pan_y"), pan_y);
        property(engine::ui::intern("zoom"), zoom);
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
        zoom.set(1.0f);
    }
};

// Counts measure_text calls so layout can assert it isn't measuring the same element's text
// more than once per layout pass (layout_stack used to call compute_used twice per flow child).
class CountingPainter final : public engine::ui::IUiPainter {
public:
    int measure_text_calls = 0;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
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
        ++measure_text_calls;
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

engine::ui::Stylesheet viewport_sheet() {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        Viewport { width: 100; height: 100; }
        Button { width: 20; height: 20; position: absolute; left: 80; top: 80; }
    )",
            warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

}

TEST(UiLayoutHit, PointerLayoutMatchesPaintWhenPainterRegistered) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    auto parsed = engine::ui::parse_xml(
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

TEST(UiLayoutHit, ViewportPanDoesNotMoveLayoutRect) {
    ViewportCameraViewModel vm;
    vm.zoom.set(1.0f);
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Viewport pan-x="{binding pan_x}" pan-y="{binding pan_y}" zoom="{binding zoom}">
                 <Button command="{binding click}" content="Go"/>
               </Viewport></Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    const engine::ui::Stylesheet sheet = viewport_sheet();
    engine::ui::apply_layout_style(parsed->root, &sheet, 100.0f, 100.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    const engine::ui::Element* button = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    const engine::render::Rect before = button->layout_rect;

    vm.pan_x.set(-60.0f);
    vm.zoom.set(2.0f);
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    engine::ui::apply_layout_style(parsed->root, &sheet, 100.0f, 100.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f});
    const engine::ui::Element* after = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Button);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->layout_rect, before);
}

TEST(UiLayoutHit, ViewportHitTestInvertsPanAndClips) {
    ViewportCameraViewModel vm;
    vm.pan_x.set(-60.0f);
    vm.zoom.set(1.0f);
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Viewport pan-x="{binding pan_x}" pan-y="{binding pan_y}" zoom="{binding zoom}">
                 <Button command="{binding click}" content="Go"/>
               </Viewport></Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    const engine::ui::Stylesheet sheet = viewport_sheet();
    engine::ui::apply_layout_style(parsed->root, &sheet, 100.0f, 100.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::Element* inside = engine::ui::hit_test(parsed->root, 20.0f, 80.0f);
    ASSERT_NE(inside, nullptr);
    EXPECT_EQ(inside->kind, engine::ui::ElementKind::Button);

    engine::ui::Element* empty = engine::ui::hit_test(parsed->root, 10.0f, 10.0f);
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->kind, engine::ui::ElementKind::Viewport);

    EXPECT_EQ(engine::ui::hit_test(parsed->root, 150.0f, 80.0f), nullptr);
}

TEST(UiLayoutHit, ViewportZoomKeepsContentUnderPointer) {
    const glm::vec2 origin{24.0f, 16.0f};
    const glm::vec2 pan{-8.0f, 12.0f};
    const glm::vec2 pointer{70.0f, 55.0f};
    const float z = 1.0f;
    const float new_z = engine::ui::kViewportZoomStep;
    const glm::vec2 layout = origin + (pointer - origin) / z - pan;
    const glm::vec2 new_pan = engine::ui::viewport_pan_after_zoom(origin, pan, z, new_z, pointer);
    const glm::vec2 displayed = engine::ui::viewport_to_display(origin, new_pan, new_z, layout);
    EXPECT_NEAR(displayed.x, pointer.x, 1e-4f);
    EXPECT_NEAR(displayed.y, pointer.y, 1e-4f);
}

TEST(UiLayoutHit, LayoutStackMeasuresEachFlowChildTextOnce) {
    engine::ui::UiDocument document;
    document.root.kind = engine::ui::ElementKind::Stack;
    document.root.direction = engine::ui::StackDirection::Vertical;
    document.root.width = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};
    document.root.height = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};

    constexpr int kLabelCount = 5;
    for (int i = 0; i < kLabelCount; ++i) {
        engine::ui::Element label;
        label.kind = engine::ui::ElementKind::Label;
        label.text = "Label " + std::to_string(i);
        document.root.children.push_back(std::move(label));
    }

    CountingPainter painter;
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f}, &painter);

    // layout_stack resolves each flow child's used size once for the packed/cross (scroll
    // extent) accumulation and once for placement; before the fix that meant two
    // measure_text calls per label per layout() pass instead of one.
    EXPECT_EQ(painter.measure_text_calls, kLabelCount);
}

TEST(UiLayoutHit, MeasureTextCacheReusedAcrossUnchangedLayoutCalls) {
    engine::ui::UiDocument document;
    document.root.kind = engine::ui::ElementKind::Stack;
    document.root.direction = engine::ui::StackDirection::Vertical;
    document.root.width = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};
    document.root.height = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};

    constexpr int kLabelCount = 5;
    for (int i = 0; i < kLabelCount; ++i) {
        engine::ui::Element label;
        label.kind = engine::ui::ElementKind::Label;
        label.text = "Label " + std::to_string(i);
        document.root.children.push_back(std::move(label));
    }

    CountingPainter painter;
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f}, &painter);
    const int calls_after_first = painter.measure_text_calls;
    EXPECT_EQ(calls_after_first, kLabelCount);

    // Same Element instances (mirrors a reconciled ItemsControl row reused by identity across
    // frames), same painter, text/font/size unchanged: the second layout() pass must be served
    // entirely from each Label's memoized Element::text_measure_cache_*, not re-shape text.
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f}, &painter);
    EXPECT_EQ(painter.measure_text_calls, calls_after_first);
}

TEST(UiLayoutHit, MeasureTextCacheInvalidatesWhenElementTextChanges) {
    engine::ui::UiDocument document;
    document.root.kind = engine::ui::ElementKind::Stack;
    document.root.direction = engine::ui::StackDirection::Vertical;
    document.root.width = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};
    document.root.height = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};

    engine::ui::Element label;
    label.kind = engine::ui::ElementKind::Label;
    label.text = "Initial";
    document.root.children.push_back(std::move(label));

    CountingPainter painter;
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f}, &painter);
    EXPECT_EQ(painter.measure_text_calls, 1);

    // A changed text (e.g. from apply_bindings picking up a new bound value) must invalidate the
    // cached measurement and force a fresh painter->measure_text call.
    document.root.children[0].text = "Changed text";
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f}, &painter);
    EXPECT_EQ(painter.measure_text_calls, 2);
}
