#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class DummyPainter final : public engine::ui::IUiPainter {
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
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

class ScrollViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::Bindable<float> scroll_pos;
    engine::ui::RelayCommand click;

    ScrollViewModel() {
        property(engine::ui::intern("scroll_pos"), scroll_pos);
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
        scroll_pos.set(0.0f);
    }
};

} // namespace

TEST(UiScroll, CssOverflowAndScrollbarPropertiesParse) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        ScrollView {
            overflow: auto;
            scrollbar-width: 10px;
            scrollbar-color: #555555 #222222;
            scrollbar-border-radius: 5px;
        }
        .custom-scroll {
            overflow-x: scroll;
            overflow-y: hidden;
            scrollbar-thumb-color: #123456;
            scrollbar-track-color: #654321;
            scrollbar-thumb-hover-color: #abcdef;
        }
    )", warnings);

    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    // Apply styles to a test element
    engine::ui::Element element;
    element.kind = engine::ui::ElementKind::ScrollView;
    engine::ui::apply_layout_style(element, &(*sheet), 800.0f, 600.0f);

    EXPECT_EQ(element.overflow_x, engine::ui::Overflow::Auto);
    EXPECT_EQ(element.overflow_y, engine::ui::Overflow::Auto);
    ASSERT_TRUE(element.scrollbar_width.has_value());
    EXPECT_FLOAT_EQ(element.scrollbar_width->value, 10.0f);
    EXPECT_FLOAT_EQ(element.scrollbar_border_radius.value, 5.0f);
    EXPECT_NEAR(element.scrollbar_thumb_color.r, 0x55 / 255.0f, 1e-3f);
    EXPECT_NEAR(element.scrollbar_track_color.r, 0x22 / 255.0f, 1e-3f);

    engine::ui::Element custom_elem;
    custom_elem.classes.push_back("custom-scroll");
    engine::ui::apply_layout_style(custom_elem, &(*sheet), 800.0f, 600.0f);
    EXPECT_EQ(custom_elem.overflow_x, engine::ui::Overflow::Scroll);
    EXPECT_EQ(custom_elem.overflow_y, engine::ui::Overflow::Hidden);
    EXPECT_NEAR(custom_elem.scrollbar_thumb_color.r, 0x12 / 255.0f, 1e-3f);
    EXPECT_NEAR(custom_elem.scrollbar_track_color.r, 0x65 / 255.0f, 1e-3f);
    EXPECT_NEAR(custom_elem.scrollbar_thumb_hover_color.r, 0xab / 255.0f, 1e-3f);
}

TEST(UiScroll, XmlParserParsesScrollViewAndAttributes) {
    ScrollViewModel vm;
    auto doc = engine::ui::parse_xml(R"(
        <Canvas>
            <ScrollView id="scroll1" overflow="auto" scroll-y="{binding scroll_pos}">
                <Button content="Item 1" command="{binding click}"/>
                <Button content="Item 2" command="{binding click}"/>
            </ScrollView>
        </Canvas>
    )", nullptr, &vm);

    ASSERT_TRUE(doc.has_value());
    const engine::ui::Element* sv = engine::ui::find_by_kind(doc->root, engine::ui::ElementKind::ScrollView);
    ASSERT_NE(sv, nullptr);
    EXPECT_EQ(sv->id, "scroll1");
    EXPECT_EQ(sv->overflow_x, engine::ui::Overflow::Auto);
    EXPECT_EQ(sv->overflow_y, engine::ui::Overflow::Auto);
    EXPECT_TRUE(is_bound(sv->scroll_y_binding));
}

TEST(UiScroll, BuilderCreatesScrollViewNode) {
    using namespace engine::ui;
    auto doc = make_document(
        canvas().add(
            scroll_view()
                .with_id("my_sv")
                .overflow_y(Overflow::Auto)
                .gap(10.0f)
                .add(button().content("Button 1"))
        )
    );

    ASSERT_TRUE(doc.has_value());
    const Element* sv = find_by_kind(doc->root, ElementKind::ScrollView);
    ASSERT_NE(sv, nullptr);
    EXPECT_EQ(sv->id, "my_sv");
    EXPECT_EQ(sv->overflow_y, Overflow::Auto);
    EXPECT_FLOAT_EQ(sv->gap.value, 10.0f);
    EXPECT_EQ(sv->children.size(), 1u);
}

TEST(UiScroll, LayoutComputesMaxScrollOnContentOverflow) {
    using namespace engine::ui;
    std::vector<std::string> warnings;
    auto sheet = parse_css(R"(
        ScrollView { width: 100px; height: 100px; overflow-y: auto; }
        Button { width: 100px; height: 40px; }
    )", warnings);
    ASSERT_TRUE(sheet.has_value());

    auto doc = make_document(
        canvas().add(
            scroll_view()
                .add(button().content("1"))
                .add(button().content("2"))
                .add(button().content("3"))
                .add(button().content("4"))
        )
    );
    ASSERT_TRUE(doc.has_value());

    apply_layout_style(doc->root, &(*sheet), 800.0f, 600.0f);
    layout(*doc, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});

    Element* sv = find_by_kind(doc->root, ElementKind::ScrollView);
    ASSERT_NE(sv, nullptr);
    // 4 buttons * 40px = 160px height. ScrollView height = 100px.
    // max_scroll_y should be 160 - 100 = 60px.
    EXPECT_FLOAT_EQ(sv->max_scroll_y, 60.0f);
    EXPECT_TRUE(is_scrollable_y(*sv));

    // Scrollbar track and thumb calculations
    const engine::render::Rect track = scrollbar_track_rect(*sv);
    EXPECT_FLOAT_EQ(track.x, sv->layout_rect.x + sv->layout_rect.w - 8.0f);
    EXPECT_FLOAT_EQ(track.y, sv->layout_rect.y);
    EXPECT_FLOAT_EQ(track.w, 8.0f);
    EXPECT_FLOAT_EQ(track.h, 100.0f);

    const engine::render::Rect thumb = scrollbar_thumb_rect(*sv);
    EXPECT_FLOAT_EQ(thumb.x, track.x);
    EXPECT_FLOAT_EQ(thumb.y, track.y); // at top when scroll_y == 0
    EXPECT_GT(thumb.h, 0.0f);
    EXPECT_LT(thumb.h, track.h);
}

TEST(UiScroll, WheelScrollsContentAndSyncsViewModel) {
    engine::ecs::World world;
    auto vm = std::make_shared<ScrollViewModel>();

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        ScrollView { width: 100px; height: 100px; overflow-y: auto; }
        Button { width: 100px; height: 50px; }
    )", warnings);
    ASSERT_TRUE(sheet.has_value());

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
            <ScrollView scroll-y="{binding scroll_pos}">
                <Button content="1" command="{binding click}"/>
                <Button content="2" command="{binding click}"/>
                <Button content="3" command="{binding click}"/>
            </ScrollView>
        </Canvas>
    )", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 800.0f, 600.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *sheet});

    engine::ui::begin_frame(world);
    // 3 buttons * 50px = 150px. Max scroll = 50px.
    // Scroll down with wheel (wheel_y = -1.0f)
    engine::ui::handle_wheel(world, 50.0f, 50.0f, -1.0f);

    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_windows.contains(engine::kPrimaryWindow));

    // ViewModel should have been updated
    EXPECT_FLOAT_EQ(vm->scroll_pos.get(), 40.0f);

    // Scroll down again: should clamp to max_scroll_y (50.0f)
    engine::ui::handle_wheel(world, 50.0f, 50.0f, -1.0f);
    EXPECT_FLOAT_EQ(vm->scroll_pos.get(), 50.0f);

    // Scroll up (wheel_y = 1.0f)
    engine::ui::handle_wheel(world, 50.0f, 50.0f, 1.0f);
    EXPECT_FLOAT_EQ(vm->scroll_pos.get(), 10.0f);
}

TEST(UiScroll, HitTestRespectsScrollOffsetAndClips) {
    engine::ecs::World world;
    auto vm = std::make_shared<ScrollViewModel>();

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        ScrollView { width: 100px; height: 100px; overflow-y: auto; }
        Button { width: 100px; height: 50px; }
    )", warnings);
    ASSERT_TRUE(sheet.has_value());

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
            <ScrollView scroll-y="{binding scroll_pos}">
                <Button id="btn1" content="1" command="{binding click}"/>
                <Button id="btn2" content="2" command="{binding click}"/>
                <Button id="btn3" content="3" command="{binding click}"/>
            </ScrollView>
        </Canvas>
    )", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 800.0f, 600.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *sheet});

    // Before scroll: btn1 is at y=0..50, btn2 is at y=50..100, btn3 is at y=100..150 (outside clip).
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 50.0f, 25.0f);
    EXPECT_EQ(vm->clicks, 1);

    // Click at y=120 (outside ScrollView): should not click anything
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 50.0f, 120.0f);
    EXPECT_EQ(vm->clicks, 1);

    // Scroll down by 50px so btn2 is at y=0..50 and btn3 is at y=50..100
    vm->scroll_pos.set(50.0f);
    engine::ui::begin_frame(world);

    // Now clicking at y=75 should click btn3!
    engine::ui::handle_pointer(world, 50.0f, 75.0f);
    EXPECT_EQ(vm->clicks, 2);
}

TEST(UiScroll, ScrollbarThumbDragUpdatesScroll) {
    engine::ecs::World world;
    auto vm = std::make_shared<ScrollViewModel>();

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        ScrollView { width: 100px; height: 100px; overflow-y: auto; scrollbar-width: 10px; }
        Button { width: 90px; height: 100px; }
    )", warnings);
    ASSERT_TRUE(sheet.has_value());

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
            <ScrollView scroll-y="{binding scroll_pos}">
                <Button content="1"/>
                <Button content="2"/>
            </ScrollView>
        </Canvas>
    )", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 800.0f, 600.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *sheet});

    engine::ui::begin_frame(world);
    // Scrollbar track is at x = 90..100, y = 0..100.
    // Thumb is at x = 90..100, y = 0..50.
    // Click on thumb to start dragging
    engine::ui::handle_pointer(world, 95.0f, 10.0f);

    // Drag down to y = 40.0f (delta +30px)
    engine::ui::update_drag(world, 95.0f, 40.0f);

    EXPECT_GT(vm->scroll_pos.get(), 0.0f);

    engine::ui::end_drag(world);
}
