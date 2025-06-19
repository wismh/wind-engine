#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

class OverlapViewModel final : public engine::ui::ViewModel {
public:
    int front_clicks = 0;
    int back_clicks = 0;
    engine::ui::RelayCommand front;
    engine::ui::RelayCommand back;

    OverlapViewModel() {
        command(engine::ui::intern("front"), front);
        command(engine::ui::intern("back"), back);
        front = [this] { ++front_clicks; };
        back = [this] { ++back_clicks; };
    }
};

class HudViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title;
    engine::ui::Bindable<int> score;
    engine::ui::RelayCommand restart;

    HudViewModel() {
        property(engine::ui::intern("title"), title);
        property(engine::ui::intern("score"), score);
        command(engine::ui::intern("restart"), restart);
    }
};

class DragViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> fraction;

    DragViewModel() { property(engine::ui::intern("fraction"), fraction); }
};

class DragAndClickViewModel final : public engine::ui::ViewModel {
public:
    int clicks = 0;
    engine::ui::Bindable<float> fraction;
    engine::ui::RelayCommand click;

    DragAndClickViewModel() {
        property(engine::ui::intern("fraction"), fraction);
        command(engine::ui::intern("click"), click);
        click = [this] { ++clicks; };
    }
};

// One volume channel's (Master/Music/SFX) own ViewModel — its `fraction` binding only exists
// here, never on LauncherViewModel below, so a drag write that (incorrectly) lands on the
// document-level VM instead of the item's own VM would silently no-op instead of changing
// anything observable. Mirrors the real repro this regression test is for (report to WindEngine,
// "drag inside ItemsControl/ItemTemplate", filed against Master/Music/SFX volume sliders built
// with an ItemsControl over one row per channel).
class VolumeChannelViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> fraction;

    VolumeChannelViewModel() { property(engine::ui::intern("fraction"), fraction); }
};

class LauncherViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<VolumeChannelViewModel>> channels;

    LauncherViewModel() { property(engine::ui::intern("channels"), channels); }
};

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
    }
};

constexpr engine::AssetId kIconGuid{"c1a1c2d3e4f5678901234567890abc09"};

engine::ui::UiCanvas make_canvas(engine::render::Rect rect, int order = 0) {
    engine::ui::UiCanvas canvas;
    canvas.rect = rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.order = order;
    return canvas;
}

template<typename T, typename = void>
struct has_onClick : std::false_type {};

template<typename T>
struct has_onClick<T, std::void_t<decltype(std::declval<T>().onClick)>> : std::true_type {};

engine::ecs::Entity spawn_button_canvas(engine::ecs::World& world, std::shared_ptr<ClickViewModel> vm,
        engine::render::Rect rect, int order) {
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)", nullptr,
            vm.get());
    EXPECT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas(rect, order);
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});
    return entity;
}

// A bare <Image>, with no `source`, still hugs to a 32x32 default box (document.cpp
// kDefaultImageSize) placed at the canvas origin — enough to hit-test against without needing a
// stylesheet to give it an explicit size. `xml` must declare its own root <Canvas>...</Canvas>.
engine::ecs::Entity spawn_canvas(engine::ecs::World& world, std::shared_ptr<engine::ui::ViewModel> vm,
        std::string_view xml, engine::render::Rect rect, int order = 0) {
    const auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
    EXPECT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas(rect, order);
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});
    return entity;
}

}

TEST(Mvvm, InternEmptyIsInvalid) {
    EXPECT_EQ(engine::ui::intern(""), engine::ui::BindingId{});
    EXPECT_EQ(engine::ui::intern("   "), engine::ui::BindingId{});
    EXPECT_EQ(engine::ui::intern("\t\n"), engine::ui::BindingId{});
}

TEST(Mvvm, InternKnownFnvFixtures) {
    static_assert(engine::ui::intern("").value == 0u);
    static_assert(engine::ui::intern("title").value == 2556802313u);
    static_assert(engine::ui::intern("score").value == 3526332565u);
    static_assert(engine::ui::intern("title") == engine::ui::intern("title"));
    static_assert(engine::ui::intern("title") != engine::ui::intern("score"));

    EXPECT_EQ(engine::ui::intern("").value, 0u);
    EXPECT_EQ(engine::ui::intern("title").value, 2556802313u);
    EXPECT_EQ(engine::ui::intern("score").value, 3526332565u);
    EXPECT_EQ(engine::ui::intern("title"), engine::ui::intern("title"));
    EXPECT_NE(engine::ui::intern("title"), engine::ui::intern("score"));
}

TEST(Mvvm, PropertyAndCommandRegistration) {
    HudViewModel vm;
    EXPECT_TRUE(vm.has_property(engine::ui::intern("title")));
    EXPECT_TRUE(vm.has_property(engine::ui::intern("score")));
    EXPECT_TRUE(vm.has_command(engine::ui::intern("restart")));
    EXPECT_FALSE(vm.has_property(engine::ui::intern("Missing")));
    EXPECT_FALSE(vm.has_command(engine::ui::intern("Missing")));

    vm.title.set("HUD");
    vm.score.set(12);
    EXPECT_EQ(vm.read_property_string(engine::ui::intern("title")), "HUD");
    EXPECT_EQ(vm.read_property_string(engine::ui::intern("score")), "12");
    EXPECT_EQ(vm.read_property_asset_id(engine::ui::intern("title")), std::nullopt);
    EXPECT_EQ(vm.read_property_asset_id(engine::ui::intern("score")), std::nullopt);
    EXPECT_NE(vm.find_command(engine::ui::intern("restart")), nullptr);
}

TEST(Mvvm, AssetIdPropertyReadsTypedIdNotHexString) {
    class IconVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<engine::AssetId> icon{kIconGuid};

        IconVm() { property(engine::ui::intern("icon"), icon); }
    };
    IconVm vm;
    EXPECT_TRUE(vm.has_property(engine::ui::intern("icon")));
    EXPECT_EQ(vm.read_property_asset_id(engine::ui::intern("icon")), kIconGuid);
    const auto as_string = vm.read_property_string(engine::ui::intern("icon"));
    ASSERT_TRUE(as_string.has_value());
    EXPECT_TRUE(as_string->empty());
}

TEST(Mvvm, ImageSourceBindingWritesAssetId) {
    class IconVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<engine::AssetId> icon{kIconGuid};

        IconVm() { property(engine::ui::intern("icon"), icon); }
    };
    IconVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image source="{binding icon}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    const engine::ui::Element* image = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->source.has_value());
    EXPECT_EQ(*image->source, kIconGuid);
}

TEST(Mvvm, ImageSourceStringPropertyIsTypeMismatch) {
    class StringIconVm final : public engine::ui::ViewModel {
    public:
        engine::ui::Bindable<std::string> icon{std::string(kIconGuid.hex())};

        StringIconVm() { property(engine::ui::intern("icon"), icon); }
    };
    StringIconVm vm;
    RecordingFatalError fatal;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image source="{binding icon}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const auto applied = engine::ui::apply_bindings(*parsed, vm, &fatal);
    EXPECT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
    const engine::ui::Element* image = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(image->source.has_value());
}

TEST(Mvvm, OneWayBindUpdatesLabelText) {
    HudViewModel vm;
    vm.title.set("Hello");

    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());

    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    const engine::ui::Element* label = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");

    vm.title.set("World");
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    EXPECT_EQ(engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Label)->text, "World");
}

TEST(Mvvm, ButtonClickExecutesWhenCanExecute) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_EQ(vm->clicks, 1);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, ButtonClickSkippedWhenCannotExecute) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    vm->click.set_can_execute(false);
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_EQ(vm->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, HoverSetsMouseConsumedWithoutExecutingCommand) {
    // click_through must not engage while the pointer merely hovers a UI element
    // (no click yet) — update_pointer_hover has to set MouseConsumed the same way handle_pointer
    // does, but never invoke the hit element's command.
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::update_pointer_hover(world, 4.0f, 4.0f);

    EXPECT_EQ(vm->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, HoverMissLeavesMouseConsumedFalse) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {10.0f, 20.0f, 50.0f, 40.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::update_pointer_hover(world, 5.0f, 5.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, MouseConsumedGoesFalseWhenPointerMovesOffWidgetWithoutAClick) {
    // The bug this fix addresses: begin_frame() resets MouseConsumed every frame, and before this
    // fix only a Down event (via handle_pointer) ever set it back to true — a frame with only Move
    // events left it permanently false, so click-through would ignore hover entirely. Hovering on
    // then off, with no click in between, must track both transitions correctly on its own.
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::update_pointer_hover(world, 4.0f, 4.0f);
    ASSERT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));

    engine::ui::begin_frame(world);
    engine::ui::update_pointer_hover(world, 500.0f, 500.0f);
    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, OnClickApiAbsent) {
    EXPECT_FALSE(has_onClick<engine::ui::Element>::value);
    EXPECT_FALSE(has_onClick<engine::ui::ViewModel>::value);
    EXPECT_FALSE(has_onClick<engine::ui::UiCanvas>::value);
    EXPECT_FALSE(has_onClick<engine::ui::ICommand>::value);
    EXPECT_FALSE(has_onClick<engine::ui::RelayCommand>::value);
    EXPECT_FALSE(has_onClick<engine::ui::UiInstance>::value);
}

TEST(Mvvm, UiCanvasHitTestInsideRect) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, make_canvas({10.0f, 20.0f, 50.0f, 40.0f}));

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 15.0f, 25.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, UiCanvasHitTestEmptyFillWindow) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({10.0f, 20.0f, 30.0f, 40.0f});
    canvas.fit = engine::ui::UiFit::FillWindow;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 15.0f, 25.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, UiCanvasHitTestLabelDoesNotConsume) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="HUD"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({0.0f, 0.0f, 100.0f, 100.0f});
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, UiCanvasHitTestMissOutsideRect) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {10.0f, 20.0f, 50.0f, 40.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 5.0f, 5.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, HigherOrderCanvasWinsHitTest) {
    engine::ecs::World world;
    auto back = std::make_shared<ClickViewModel>();
    auto front = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, back, {0.0f, 0.0f, 100.0f, 100.0f}, 0);
    spawn_button_canvas(world, front, {0.0f, 0.0f, 100.0f, 100.0f}, 1);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_EQ(front->clicks, 1);
    EXPECT_EQ(back->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, WriteAndReadPropertyFloatRoundTripOnArithmeticProperty) {
    HudViewModel vm;
    EXPECT_TRUE(vm.write_property_float(engine::ui::intern("score"), 42.0f));
    EXPECT_EQ(vm.score.get(), 42);
    ASSERT_TRUE(vm.read_property_float(engine::ui::intern("score")).has_value());
    EXPECT_FLOAT_EQ(*vm.read_property_float(engine::ui::intern("score")), 42.0f);
}

TEST(Mvvm, WritePropertyFloatNoOpsOnNonArithmeticProperty) {
    HudViewModel vm;
    vm.title.set("unchanged");
    EXPECT_FALSE(vm.write_property_float(engine::ui::intern("title"), 1.0f));
    EXPECT_EQ(vm.title.get(), "unchanged");
    EXPECT_FALSE(vm.read_property_float(engine::ui::intern("title")).has_value());
}

TEST(Mvvm, WritePropertyFloatNoOpsOnUnregisteredBinding) {
    HudViewModel vm;
    EXPECT_FALSE(vm.write_property_float(engine::ui::intern("nope"), 1.0f));
}

TEST(Mvvm, UnboundImageDoesNotConsume) {
    // The widened hit_test() (Button, or any kind with a bound command/drag) must not turn a
    // plain, uninteractive Image into a hit-target just because it now hugs to a nonzero default
    // size — MouseConsumed should stay exactly as false as it already is for an unbound Label.
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, ImageWithCommandFiresOnClick) {
    // Proves the hit_test() generalization isn't drag-specific: a plain Image (not a Button) with
    // a bound `command` is now a real hit-target too.
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image command="{binding click}"/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);

    EXPECT_EQ(vm->clicks, 1);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, DragDownWritesClampedFractionAlongHorizontalAxis) {
    // The Image's default 32x32 hug box sits at the canvas origin (document.cpp
    // kDefaultImageSize); clicking at local x=8 is 8/32 of the way across it.
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);

    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.25f);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, DragVerticalOrientationUsesYAxis) {
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image drag="{binding fraction}" drag-orientation="vertical"/></Canvas>)",
            {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    // x near the far edge is ignored on the vertical axis; y = half the 32px box.
    engine::ui::handle_pointer(world, 30.0f, 16.0f);

    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.5f);
}

TEST(Mvvm, DragContinuesTrackingAfterPointerLeavesElementBounds) {
    // The key behavior distinguishing update_drag() from plain hit-testing: the drag keeps
    // updating from its captured start geometry even once (x, y) is nowhere near the element.
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    ASSERT_FLOAT_EQ(vm->fraction.get(), 0.25f);

    engine::ui::update_drag(world, 5000.0f, 5000.0f);
    EXPECT_FLOAT_EQ(vm->fraction.get(), 1.0f);

    engine::ui::update_drag(world, -5000.0f, -5000.0f);
    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.0f);
}

TEST(Mvvm, EndDragStopsFurtherUpdates) {
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    ASSERT_FLOAT_EQ(vm->fraction.get(), 0.25f);

    engine::ui::end_drag(world);
    engine::ui::update_drag(world, 5000.0f, 5000.0f);

    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.25f);
}

TEST(Mvvm, UpdateDragIsNoOpWithoutAPriorDown) {
    engine::ecs::World world;
    auto vm = std::make_shared<DragViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)", {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::update_drag(world, 8.0f, 8.0f);

    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.0f);
}

TEST(Mvvm, DragAndCommandBothBoundBothFireOnSameDown) {
    engine::ecs::World world;
    auto vm = std::make_shared<DragAndClickViewModel>();
    spawn_canvas(world, vm, R"(<Canvas><Image command="{binding click}" drag="{binding fraction}"/></Canvas>)",
            {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);

    EXPECT_EQ(vm->clicks, 1);
    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.25f);
}

TEST(Mvvm, DragIsIsolatedPerWindow) {
    engine::ecs::World world;
    const engine::WindowId window_a = engine::kPrimaryWindow;
    const engine::WindowId window_b{5};

    auto vm_a = std::make_shared<DragViewModel>();
    const engine::ecs::Entity entity_a =
            spawn_canvas(world, vm_a, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)",
                    {0.0f, 0.0f, 100.0f, 100.0f});
    world.get<engine::ui::UiCanvas>(entity_a).window = window_a;

    auto vm_b = std::make_shared<DragViewModel>();
    const engine::ecs::Entity entity_b =
            spawn_canvas(world, vm_b, R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)",
                    {0.0f, 0.0f, 100.0f, 100.0f});
    world.get<engine::ui::UiCanvas>(entity_b).window = window_b;

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f, window_a);
    engine::ui::handle_pointer(world, 16.0f, 8.0f, window_b);

    ASSERT_FLOAT_EQ(vm_a->fraction.get(), 0.25f);
    ASSERT_FLOAT_EQ(vm_b->fraction.get(), 0.5f);

    engine::ui::update_drag(world, 32.0f, 8.0f, window_a);
    EXPECT_FLOAT_EQ(vm_a->fraction.get(), 1.0f);
    EXPECT_FLOAT_EQ(vm_b->fraction.get(), 0.5f);
}

constexpr std::string_view kVolumeChannelDragXml = R"(
<Canvas>
  <ItemsControl items_source="{binding channels}">
    <ItemTemplate>
      <Stack direction="vertical">
        <Image drag="{binding fraction}"/>
      </Stack>
    </ItemTemplate>
  </ItemsControl>
</Canvas>
)";

TEST(Mvvm, DragInsideItemsControlWritesToItsOwnItemViewModelNotTheDocument) {
    // Regression test for the bug reported against wind-114 (engine pin 79c6c79): a `drag`
    // binding on an ItemTemplate-generated element used to always write through the canvas's own
    // data_context, which never has the item-level property registered — the write silently
    // no-op'd. This is the report's own repro shape: one ItemsControl row per volume channel
    // (Master/Music/SFX), each with its own `fraction`. LauncherViewModel below deliberately has
    // no "fraction" property at all, so this can only pass if the write reaches the right
    // VolumeChannelViewModel.
    engine::ecs::World world;
    auto vm = std::make_shared<LauncherViewModel>();
    auto master = std::make_shared<VolumeChannelViewModel>();
    auto music = std::make_shared<VolumeChannelViewModel>();
    auto sfx = std::make_shared<VolumeChannelViewModel>();
    vm->channels.set({master, music, sfx});
    spawn_canvas(world, vm, kVolumeChannelDragXml, {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    // ItemsControl stacks generated items vertically with the default 32x32 Image hug box each;
    // music's row sits at {0, 32, 32, 32}. x=8 within it is 8/32 = 0.25 on the default
    // horizontal drag axis.
    engine::ui::handle_pointer(world, 8.0f, 40.0f);

    EXPECT_FLOAT_EQ(music->fraction.get(), 0.25f);
    EXPECT_FLOAT_EQ(master->fraction.get(), 0.0f);
    EXPECT_FLOAT_EQ(sfx->fraction.get(), 0.0f);
}

TEST(Mvvm, DragInsideItemsControlContinuesAcrossMoveEvents) {
    // Proves update_drag()'s owner re-resolution path, not just handle_pointer()'s drag-start
    // write: the drag must keep targeting master's ViewModel on later Move events too, including
    // once (x, y) is far outside its bounds.
    engine::ecs::World world;
    auto vm = std::make_shared<LauncherViewModel>();
    auto master = std::make_shared<VolumeChannelViewModel>();
    auto music = std::make_shared<VolumeChannelViewModel>();
    vm->channels.set({master, music});
    spawn_canvas(world, vm, kVolumeChannelDragXml, {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    ASSERT_FLOAT_EQ(master->fraction.get(), 0.25f);

    engine::ui::update_drag(world, 5000.0f, 5000.0f);

    EXPECT_FLOAT_EQ(master->fraction.get(), 1.0f);
    EXPECT_FLOAT_EQ(music->fraction.get(), 0.0f);
}

TEST(Mvvm, DragInsideItemsControlStopsWritingWhenItemRemovedMidDrag) {
    // Not itself part of the reported repro — a general robustness guard for any ItemsControl
    // whose items_source can shrink mid-drag. update_drag() must re-validate the captured owner
    // against a freshly re-bound tree every frame rather than holding onto (and dereferencing) a
    // ViewModel* across frames unconditionally.
    engine::ecs::World world;
    auto vm = std::make_shared<LauncherViewModel>();
    auto master = std::make_shared<VolumeChannelViewModel>();
    auto music = std::make_shared<VolumeChannelViewModel>();
    vm->channels.set({master, music});
    spawn_canvas(world, vm, kVolumeChannelDragXml, {0.0f, 0.0f, 100.0f, 100.0f});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    ASSERT_FLOAT_EQ(master->fraction.get(), 0.25f);

    // master's row is removed from the list mid-drag; music slides up to occupy its old
    // on-screen position once bindings/layout next run.
    vm->channels.set({music});
    engine::ui::update_drag(world, 5000.0f, 5000.0f);

    // The stale drag must not keep writing into the removed item...
    EXPECT_FLOAT_EQ(master->fraction.get(), 0.25f);
    // ...nor misattribute its update to whatever item now occupies that screen position.
    EXPECT_FLOAT_EQ(music->fraction.get(), 0.0f);

    // Dropping the test's own last reference actually destroys the removed item; update_drag()
    // must not still be holding (and now dereferencing) that freed pointer.
    master.reset();
    engine::ui::update_drag(world, 1.0f, 1.0f);
    engine::ui::end_drag(world);
}

TEST(Mvvm, ElementZIndexWinsHitTestWithinSameCanvas) {
    // "back" is first in document order (would win today's document-order-only hit-test) but
    // gets a higher z-index via CSS, so it must win the click despite being written first.
    engine::ecs::World world;
    auto vm = std::make_shared<OverlapViewModel>();
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Button class="back" command="{binding back}" content="Back"/>)"
            R"(<Button class="front" command="{binding front}" content="Front"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".back { z-index: 5; } .front { z-index: 1; }", warnings);
    ASSERT_TRUE(sheet.has_value());

    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({0.0f, 0.0f, 100.0f, 100.0f}, 0);
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    engine::ui::UiInstance instance{*parsed};
    instance.stylesheet = *sheet;
    world.emplace<engine::ui::UiInstance>(entity, instance);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_EQ(vm->back_clicks, 1);
    EXPECT_EQ(vm->front_clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, RotatedButtonAabbIsClickableOutsideUnrotatedRect) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Button class="spin" command="{binding click}" content="X"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".spin { width: 20; height: 20; transform: rotate(45); }", warnings);
    ASSERT_TRUE(sheet.has_value());

    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({0.0f, 0.0f, 100.0f, 100.0f}, 0);
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    engine::ui::UiInstance instance{*parsed};
    instance.stylesheet = *sheet;
    world.emplace<engine::ui::UiInstance>(entity, instance);

    engine::ui::begin_frame(world);
    // The button is a 20x20 square at (0,0), center (10,10) - unrotated it ends at x=20.
    // Rotated 45deg its AABB half-diagonal is 10*sqrt(2) =~ 14.14, so x=22 (outside the
    // unrotated rect) falls inside the rotated AABB and must still register the click.
    engine::ui::handle_pointer(world, 22.0f, 10.0f);

    EXPECT_EQ(vm->clicks, 1);
}

TEST(Mvvm, FrontCanvasMissDoesNotFallThrough) {
    engine::ecs::World world;
    auto back = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, back, {0.0f, 0.0f, 100.0f, 100.0f}, 0);
    const engine::ecs::Entity front = world.create();
    world.emplace<engine::ui::UiCanvas>(front, make_canvas({0.0f, 0.0f, 100.0f, 100.0f}, 1));

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

    EXPECT_EQ(back->clicks, 0);
    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, MouseConsumedResetOnBeginFrame) {
    engine::ecs::World world;
    world.ctx<engine::ui::MouseConsumed>().consumed_windows.insert(engine::kPrimaryWindow);
    engine::ui::begin_frame(world);
    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(Mvvm, FillWindowResizeWritesRect) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({10.0f, 20.0f, 30.0f, 40.0f});
    canvas.fit = engine::ui::UiFit::FillWindow;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);

    EXPECT_EQ(world.get<engine::ui::UiCanvas>(entity).rect, (engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f}));
}

TEST(Mvvm, FixedFitLeavesRectAlone) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    const engine::render::Rect original{10.0f, 20.0f, 30.0f, 40.0f};
    engine::ui::UiCanvas canvas = make_canvas(original);
    canvas.fit = engine::ui::UiFit::Fixed;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);

    EXPECT_EQ(world.get<engine::ui::UiCanvas>(entity).rect, original);
}

TEST(Mvvm, ScaleWithScreenSizeResizeWritesLetterboxedRect) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({});
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = {200.0f, 100.0f};
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);

    // scale = min(800/200, 600/100) = min(4, 6) = 4; scaled box is 800x400, letterboxed vertically.
    EXPECT_EQ(world.get<engine::ui::UiCanvas>(entity).rect, (engine::render::Rect{0.0f, 100.0f, 800.0f, 400.0f}));
}

TEST(Mvvm, ScaleWithScreenSizeWithoutReferenceSizeFallsBackToWindow) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({});
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);

    EXPECT_EQ(world.get<engine::ui::UiCanvas>(entity).rect, (engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f}));
}

TEST(Mvvm, ScaleWithScreenSizeRejectsClicksInLetterboxBar) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    const engine::ecs::Entity entity = world.create();
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiCanvas canvas = make_canvas({});
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = {200.0f, 100.0f};
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);
    // Viewport is {0,100,800,400} (see ScaleWithScreenSizeResizeWritesLetterboxedRect); y=5 is in the top bar.
    engine::ui::handle_pointer(world, 5.0f, 5.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, ScaleWithScreenSizeHitTestScalesPointerIntoDesignSpace) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    const engine::ecs::Entity entity = world.create();
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiCanvas canvas = make_canvas({});
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = {200.0f, 100.0f};
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    engine::ui::begin_frame(world);
    // Viewport {0,100,800,400}, scale 4. Real (16,116) maps to design (4,4), inside the default-sized button.
    engine::ui::handle_pointer(world, 16.0f, 116.0f);

    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
    EXPECT_EQ(vm->clicks, 1);
}

TEST(Mvvm, WindowSizeForPrimaryReadsWindowSizesCtx) {
    engine::ecs::World world;
    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};

    const engine::ui::WindowSize size = engine::ui::window_size_for(world, engine::kPrimaryWindow);
    EXPECT_EQ(size.width, 800);
    EXPECT_EQ(size.height, 600);
}

TEST(Mvvm, WindowSizeForUnsizedSecondaryWindowDefaultsToZero) {
    engine::ecs::World world;
    const engine::WindowId other{7};

    const engine::ui::WindowSize size = engine::ui::window_size_for(world, other);
    EXPECT_EQ(size.width, 0);
    EXPECT_EQ(size.height, 0);
}

TEST(Mvvm, WindowSizeForSecondaryWindowReadsWindowSizesCtx) {
    engine::ecs::World world;
    const engine::WindowId other{7};
    world.ctx<engine::ui::WindowSizes>().sizes[other] = engine::ui::WindowSize{320, 240};

    const engine::ui::WindowSize size = engine::ui::window_size_for(world, other);
    EXPECT_EQ(size.width, 320);
    EXPECT_EQ(size.height, 240);
}

TEST(Mvvm, PointerForPrimaryReadsUiPointerCtx) {
    engine::ecs::World world;
    world.ctx<engine::ui::UiPointer>().position = {12.0f, 34.0f};
    world.ctx<engine::ui::UiPointer>().down = true;

    const engine::ui::UiPointer& pointer = engine::ui::pointer_for(world, engine::kPrimaryWindow);
    EXPECT_EQ(pointer.position, (glm::vec2{12.0f, 34.0f}));
    EXPECT_TRUE(pointer.down);
}

TEST(Mvvm, PointerForSecondaryWindowIsIsolatedFromPrimary) {
    engine::ecs::World world;
    const engine::WindowId other{7};
    world.ctx<engine::ui::UiPointer>().position = {12.0f, 34.0f};
    world.ctx<engine::ui::UiPointer>().down = true;

    // Never written for `other` — must not see the primary's position/down state.
    const engine::ui::UiPointer& pointer = engine::ui::pointer_for(world, other);
    EXPECT_EQ(pointer.position, (glm::vec2{0.0f, 0.0f}));
    EXPECT_FALSE(pointer.down);

    engine::ui::pointer_for(world, other).position = {5.0f, 6.0f};
    engine::ui::pointer_for(world, other).down = true;

    EXPECT_EQ(engine::ui::pointer_for(world, other).position, (glm::vec2{5.0f, 6.0f}));
    EXPECT_TRUE(engine::ui::pointer_for(world, other).down);
    // The primary's own pointer must be untouched by writes aimed at `other`.
    EXPECT_EQ(world.ctx<engine::ui::UiPointer>().position, (glm::vec2{12.0f, 34.0f}));
}

TEST(Mvvm, FillWindowCanvasesEachFollowTheirOwnWindowSize) {
    engine::ecs::World world;
    const engine::WindowId secondary{9};

    const engine::ecs::Entity primary_entity = world.create();
    engine::ui::UiCanvas primary_canvas = make_canvas({});
    primary_canvas.fit = engine::ui::UiFit::FillWindow;
    primary_canvas.window = engine::kPrimaryWindow;
    world.emplace<engine::ui::UiCanvas>(primary_entity, primary_canvas);

    const engine::ecs::Entity secondary_entity = world.create();
    engine::ui::UiCanvas secondary_canvas = make_canvas({});
    secondary_canvas.fit = engine::ui::UiFit::FillWindow;
    secondary_canvas.window = secondary;
    world.emplace<engine::ui::UiCanvas>(secondary_entity, secondary_canvas);

    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::ui::WindowSizes>().sizes[secondary] = engine::ui::WindowSize{320, 240};
    engine::ui::apply_canvas_fit(world);

    EXPECT_EQ(world.get<engine::ui::UiCanvas>(primary_entity).rect, (engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f}));
    EXPECT_EQ(world.get<engine::ui::UiCanvas>(secondary_entity).rect, (engine::render::Rect{0.0f, 0.0f, 320.0f, 240.0f}));
}

TEST(Mvvm, HandlePointerOnlyHitTestsCanvasesOnItsOwnWindow) {
    engine::ecs::World world;
    const engine::WindowId window_a = engine::kPrimaryWindow;
    const engine::WindowId window_b{5};

    auto vm_a = std::make_shared<ClickViewModel>();
    const engine::ecs::Entity entity_a = spawn_button_canvas(world, vm_a, {0.0f, 0.0f, 100.0f, 100.0f}, 0);
    world.get<engine::ui::UiCanvas>(entity_a).window = window_a;

    auto vm_b = std::make_shared<ClickViewModel>();
    const engine::ecs::Entity entity_b = spawn_button_canvas(world, vm_b, {0.0f, 0.0f, 100.0f, 100.0f}, 0);
    world.get<engine::ui::UiCanvas>(entity_b).window = window_b;

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f, window_a);

    EXPECT_EQ(vm_a->clicks, 1);
    EXPECT_EQ(vm_b->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

