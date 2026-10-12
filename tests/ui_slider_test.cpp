#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fixtures/bind_error_fakes.h"
#include "ui/bind_scan.h"
#include "ui/painter.h"

namespace {

class LevelViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> level;
    engine::ui::Bindable<std::string> label;

    LevelViewModel() {
        property(engine::ui::intern("level"), level);
        property(engine::ui::intern("label"), label);
    }
};

class ChannelViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> level;

    ChannelViewModel() { property(engine::ui::intern("level"), level); }
};

class MixerViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<ChannelViewModel>> channels;

    MixerViewModel() { property(engine::ui::intern("channels"), channels); }
};

// The handle is 20 px wide and placed by the stylesheet from `--fraction`; the 20 lives in one custom property.
constexpr std::string_view kHorizontalCss = R"(
.vol { width: 200; height: 20; }
.slider-handle { --handle: 20px; position: absolute; top: 0; width: var(--handle); height: 20;
                 left: calc(var(--fraction) * (100% - var(--handle))); }
.slider-fill { position: absolute; left: 0; top: 0; height: 20; width: calc(var(--fraction) * 100%); }
)";
constexpr std::string_view kVerticalCss = R"(
.vol { width: 20; height: 200; }
.slider-handle { --handle: 20px; position: absolute; left: 0; width: 20; height: var(--handle);
                 bottom: calc(var(--fraction) * (100% - var(--handle))); }
)";

engine::ui::Stylesheet parse_sheet(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());
    return std::move(*sheet);
}

engine::ui::Element* find_class(engine::ui::Element& root, std::string_view name) {
    if (std::ranges::find(root.classes, name) != root.classes.end()) {
        return &root;
    }
    for (engine::ui::Element& child : root.children) {
        if (auto* found = find_class(child, name)) {
            return found;
        }
    }
    return nullptr;
}

void frame(engine::ui::UiDocument& document, engine::ui::ViewModel& vm, const engine::ui::Stylesheet& sheet) {
    ASSERT_TRUE(engine::ui::apply_bindings(document, vm).has_value());
    engine::ui::apply_layout_style(document.root, &sheet, 400.0f, 400.0f);
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
}

// A canvas the pointer functions can drive: the slider sits at the canvas origin.
struct Board {
    engine::ecs::World world;
    std::shared_ptr<engine::ui::ViewModel> vm;
    engine::ecs::Entity entity;

    Board(std::string_view xml, std::string_view css, std::shared_ptr<engine::ui::ViewModel> view_model)
        : vm(std::move(view_model)) {
        auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
        EXPECT_TRUE(parsed.has_value());
        entity = world.create();
        engine::ui::UiCanvas canvas;
        canvas.rect = {0.0f, 0.0f, 400.0f, 400.0f};
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.data_context = vm;
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(
                entity, engine::ui::UiInstance{.document = std::move(*parsed), .stylesheet = parse_sheet(css)});
        engine::ui::begin_frame(world);
    }

    engine::ui::UiDocument& document() { return world.get<engine::ui::UiInstance>(entity).document; }
};

constexpr std::string_view kSliderXml =
        R"(<Canvas><Slider class="vol" value="{binding level}" min="0" max="100" step="5"/></Canvas>)";
constexpr std::string_view kContinuousXml =
        R"(<Canvas><Slider class="vol" value="{binding level}" min="0" max="1"/></Canvas>)";

}

TEST(UiSlider, ParsesTheRangeAndGeneratesItsParts) {
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Slider value="{binding level}" min="-10" max="10" step="2.5"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element& slider = parsed->root.children[0];
    EXPECT_EQ(slider.kind, engine::ui::ElementKind::Slider);
    EXPECT_FLOAT_EQ(slider.slider_min, -10.0f);
    EXPECT_FLOAT_EQ(slider.slider_max, 10.0f);
    EXPECT_FLOAT_EQ(slider.slider_step, 2.5f);
    EXPECT_EQ(slider.drag_binding, engine::ui::intern("level"));
    ASSERT_EQ(slider.children.size(), 2u);
    EXPECT_EQ(slider.children[0].classes, std::vector<std::string>{"slider-fill"});
    EXPECT_EQ(slider.children[1].classes, std::vector<std::string>{"slider-handle"});
}

TEST(UiSlider, DefaultRangeIsZeroToOneContinuous) {
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Slider value="{binding level}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FLOAT_EQ(parsed->root.children[0].slider_min, 0.0f);
    EXPECT_FLOAT_EQ(parsed->root.children[0].slider_max, 1.0f);
    EXPECT_FLOAT_EQ(parsed->root.children[0].slider_step, 0.0f);
}

TEST(UiSlider, XmlChildrenAreIgnoredBecauseThePartsAreGenerated) {
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Slider value="{binding level}"><Label text="x"/></Slider></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->root.children[0].children.size(), 2u);
}

TEST(UiSlider, MissingValueAndBadRangesAreParseErrors) {
    EXPECT_EQ(engine::ui::parse_xml(R"(<Canvas><Slider/></Canvas>)").error(), engine::ui::UiError::MissingBinding);
    EXPECT_EQ(engine::ui::parse_xml(R"(<Canvas><Slider value="1"/></Canvas>)").error(),
            engine::ui::UiError::MissingBinding);
    EXPECT_EQ(engine::ui::parse_xml(R"(<Canvas><Slider value="{binding v}" min="5" max="5"/></Canvas>)").error(),
            engine::ui::UiError::InvalidMarkup);
    EXPECT_EQ(engine::ui::parse_xml(R"(<Canvas><Slider value="{binding v}" min="5" max="1"/></Canvas>)").error(),
            engine::ui::UiError::InvalidMarkup);
    EXPECT_EQ(engine::ui::parse_xml(R"(<Canvas><Slider value="{binding v}" step="-1"/></Canvas>)").error(),
            engine::ui::UiError::InvalidMarkup);
}

TEST(UiSlider, ValueToHandleRectAtMinMidMaxAndStep) {
    auto parsed = engine::ui::parse_xml(kSliderXml);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(kHorizontalCss);
    LevelViewModel vm;

    struct Case {
        float value;
        float handle_x;
        float fill_w;
    };
    // Travel is 200 less the 20 px handle: 180 px.
    for (const Case c : {Case{0.0f, 0.0f, 0.0f}, Case{50.0f, 90.0f, 100.0f}, Case{100.0f, 180.0f, 200.0f},
                 Case{25.0f, 45.0f, 50.0f}, Case{-40.0f, 0.0f, 0.0f}, Case{250.0f, 180.0f, 200.0f}}) {
        vm.level.set(c.value);
        frame(*parsed, vm, sheet);
        const engine::ui::Element& slider = *find_class(parsed->root, "vol");
        const engine::ui::Element& handle = slider.children[1];
        EXPECT_FLOAT_EQ(handle.layout_rect.x, c.handle_x) << c.value;
        EXPECT_FLOAT_EQ(handle.layout_rect.w, 20.0f) << c.value;
        EXPECT_FLOAT_EQ(slider.children[0].layout_rect.w, c.fill_w) << c.value;
    }
}

TEST(UiSlider, FractionIsOnTheSliderAndBothPartsAsACustomProperty) {
    auto parsed = engine::ui::parse_xml(kSliderXml);
    ASSERT_TRUE(parsed.has_value());
    LevelViewModel vm;
    vm.level.set(25.0f);
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Element& slider = parsed->root.children[0];
    EXPECT_EQ(slider.custom_properties.at("fraction"), "0.250000");
    EXPECT_EQ(slider.children[0].custom_properties.at("fraction"), "0.250000");
    EXPECT_EQ(slider.children[1].custom_properties.at("fraction"), "0.250000");
}

TEST(UiSlider, PointerToValueAtMinMidMaxWithStep) {
    auto vm = std::make_shared<LevelViewModel>();
    Board board(kSliderXml, kHorizontalCss, vm);

    // The handle's centre follows the pointer: x 10 is the track's start (value 0), x 190 its end (100).
    engine::ui::handle_pointer(board.world, 10.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 0.0f);
    engine::ui::update_drag(board.world, 100.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 50.0f);
    engine::ui::update_drag(board.world, 190.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 100.0f);

    // Past either end it stays at the end, also off the track.
    engine::ui::update_drag(board.world, -300.0f, 400.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 0.0f);
    engine::ui::update_drag(board.world, 900.0f, -50.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 100.0f);

    // Step 5: 69.4 px is a third of the way (value 33.3), which snaps to 35.
    engine::ui::update_drag(board.world, 10.0f + 0.33f * 180.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 35.0f);
    engine::ui::update_drag(board.world, 10.0f + 0.31f * 180.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 30.0f);
}

TEST(UiSlider, ContinuousSliderWritesTheRawValue) {
    auto vm = std::make_shared<LevelViewModel>();
    Board board(kContinuousXml, kHorizontalCss, vm);

    engine::ui::handle_pointer(board.world, 10.0f + 45.0f, 10.0f);
    EXPECT_NEAR(vm->level.get(), 0.25f, 1e-5f);
}

TEST(UiSlider, ValueToRectAndRectToValueRoundTrip) {
    auto vm = std::make_shared<LevelViewModel>();
    Board board(kSliderXml, kHorizontalCss, vm);

    for (const float value : {0.0f, 5.0f, 35.0f, 50.0f, 95.0f, 100.0f}) {
        vm->level.set(value);
        ASSERT_TRUE(engine::ui::apply_bindings(board.document(), *vm).has_value());
        engine::ui::apply_layout_style(board.document().root, &*board.world.get<engine::ui::UiInstance>(board.entity)
                .stylesheet, 400.0f, 400.0f);
        engine::ui::layout(board.document(), engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
        const engine::ui::Element& handle = board.document().root.children[0].children[1];
        const float centre = handle.layout_rect.x + handle.layout_rect.w * 0.5f;

        // Pointing at the handle's centre writes the value that placed it.
        vm->level.set(-1.0f);
        engine::ui::handle_pointer(board.world, centre, 10.0f);
        EXPECT_FLOAT_EQ(vm->level.get(), value);
        engine::ui::end_drag(board.world);
    }
}

TEST(UiSlider, VerticalSliderGrowsUpward) {
    auto vm = std::make_shared<LevelViewModel>();
    Board board(R"(<Canvas><Slider class="vol" value="{binding level}" min="0" max="100" drag-orientation="vertical"/></Canvas>)",
            kVerticalCss, vm);

    // Track y 0..200, handle 20 high: the centre travels 10..190, top is the maximum.
    engine::ui::handle_pointer(board.world, 10.0f, 190.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 0.0f);
    engine::ui::update_drag(board.world, 10.0f, 100.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 50.0f);
    engine::ui::update_drag(board.world, 10.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 100.0f);

    // And the handle is placed from the bottom the same way.
    vm->level.set(50.0f);
    ASSERT_TRUE(engine::ui::apply_bindings(board.document(), *vm).has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(kVerticalCss);
    engine::ui::apply_layout_style(board.document().root, &sheet, 400.0f, 400.0f);
    engine::ui::layout(board.document(), engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
    const engine::ui::Element& handle = board.document().root.children[0].children[1];
    EXPECT_FLOAT_EQ(handle.layout_rect.y, 90.0f);
}

TEST(UiSlider, ADragKeepsWritingOutsideTheTrackUntilReleased) {
    auto vm = std::make_shared<LevelViewModel>();
    Board board(kSliderXml, kHorizontalCss, vm);

    engine::ui::handle_pointer(board.world, 100.0f, 10.0f);
    ASSERT_FLOAT_EQ(vm->level.get(), 50.0f);
    engine::ui::update_drag(board.world, 5000.0f, 5000.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 100.0f);

    engine::ui::end_drag(board.world);
    engine::ui::update_drag(board.world, 10.0f, 10.0f);
    EXPECT_FLOAT_EQ(vm->level.get(), 100.0f);
}

TEST(UiSlider, ValueChangeIsReportedOncePerStep) {
    auto vm = std::make_shared<LevelViewModel>();
    int changes = 0;
    vm->level.on_changed([&changes](const float&) { ++changes; });
    Board board(kSliderXml, kHorizontalCss, vm);

    engine::ui::handle_pointer(board.world, 100.0f, 10.0f);
    EXPECT_EQ(changes, 1);
    // Inside the same 5-step bucket: the snapped value is the same, so nothing is reported.
    engine::ui::update_drag(board.world, 101.0f, 10.0f);
    engine::ui::update_drag(board.world, 102.0f, 10.0f);
    EXPECT_EQ(changes, 1);
    engine::ui::update_drag(board.world, 130.0f, 10.0f);
    EXPECT_EQ(changes, 2);
}

TEST(UiSlider, EachRowOfAnItemsControlHasItsOwnSlider) {
    auto vm = std::make_shared<MixerViewModel>();
    auto master = std::make_shared<ChannelViewModel>();
    auto music = std::make_shared<ChannelViewModel>();
    vm->channels.set({master, music});
    Board board(R"(<Canvas><ItemsControl class="rows" items_source="{binding channels}"><ItemTemplate>
<Slider class="vol" value="{binding level}" min="0" max="100"/></ItemTemplate></ItemsControl></Canvas>)",
            std::string(kHorizontalCss) + ".rows { width: 200; }", vm);

    // The second row sits below the first: y 20..40.
    engine::ui::handle_pointer(board.world, 100.0f, 30.0f);
    EXPECT_FLOAT_EQ(music->level.get(), 50.0f);
    EXPECT_FLOAT_EQ(master->level.get(), 0.0f);
    engine::ui::end_drag(board.world);

    engine::ui::handle_pointer(board.world, 190.0f, 10.0f);
    EXPECT_FLOAT_EQ(master->level.get(), 100.0f);
    EXPECT_FLOAT_EQ(music->level.get(), 50.0f);
}

TEST(UiSlider, MissingValueNameIsANamedError) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Slider value="{binding nope}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    LevelViewModel vm;
    fakes::RecordingBindErrors errors;

    EXPECT_FALSE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());

    ASSERT_EQ(errors.messages.size(), 1u);
    EXPECT_EQ(errors.messages[0], "UI value binding \"nope\" is not registered on the data context");
}

TEST(UiSlider, ANonNumericValueIsANamedError) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Slider value="{binding label}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    LevelViewModel vm;
    fakes::RecordingBindErrors errors;

    EXPECT_FALSE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());

    ASSERT_EQ(errors.messages.size(), 1u);
    EXPECT_EQ(errors.messages[0], "UI value binding \"label\" must be a Bindable<float> (or another number)");
}

TEST(UiSlider, AMissingDragNameIsNowReportedToo) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image drag="{binding nope}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    LevelViewModel vm;
    fakes::RecordingBindErrors errors;

    EXPECT_FALSE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());
    ASSERT_EQ(errors.messages.size(), 1u);
    EXPECT_EQ(errors.messages[0], "UI drag binding \"nope\" is not registered on the data context");
}

TEST(UiSlider, BuilderSliderMatchesTheXmlTree) {
    auto document = engine::ui::make_document(engine::ui::canvas().add(
            engine::ui::slider().with_class("vol").drag_bind(engine::ui::intern("level")).slider_range(0.0f, 100.0f, 5.0f)));
    ASSERT_TRUE(document.has_value());
    const engine::ui::Element& slider = document->root.children[0];
    EXPECT_EQ(slider.kind, engine::ui::ElementKind::Slider);
    EXPECT_FLOAT_EQ(slider.slider_max, 100.0f);
    EXPECT_FLOAT_EQ(slider.slider_step, 5.0f);
    ASSERT_EQ(slider.children.size(), 2u);
    EXPECT_EQ(slider.children[1].classes, std::vector<std::string>{"slider-handle"});

    EXPECT_FALSE(engine::ui::make_document(engine::ui::canvas().add(engine::ui::slider().slider_range(2.0f, 1.0f)))
                         .has_value());
}

TEST(UiSlider, ScanFindsTheValueBindingAsAProperty) {
    const auto binder = engine::ui::scan_bind_tree(R"(<Canvas><Slider value="{binding level}"/></Canvas>)");
    ASSERT_TRUE(binder.has_value());
    ASSERT_EQ(binder->members.size(), 1u);
    EXPECT_EQ(binder->members[0].path, "level");
    EXPECT_EQ(binder->members[0].kind, engine::ui::BindKind::Property);
}
