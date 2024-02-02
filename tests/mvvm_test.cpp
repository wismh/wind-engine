#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

constexpr std::string_view kDummyGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

engine::ui::UiCanvas make_canvas(engine::render::Rect rect, int order = 0) {
    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kDummyGuid};
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
    EXPECT_NE(vm.find_command(engine::ui::intern("restart")), nullptr);
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
    engine::ui::handle_pointer(world, 50.0f, 50.0f);

    EXPECT_EQ(vm->clicks, 1);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
}

TEST(Mvvm, ButtonClickSkippedWhenCannotExecute) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    vm->click.set_can_execute(false);
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 50.0f, 50.0f);

    EXPECT_EQ(vm->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
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
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {10.0f, 20.0f, 50.0f, 40.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 15.0f, 25.0f);

    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
    EXPECT_EQ(vm->clicks, 1);
}

TEST(Mvvm, UiCanvasHitTestMissOutsideRect) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, vm, {10.0f, 20.0f, 50.0f, 40.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 5.0f, 5.0f);

    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().value);
    EXPECT_EQ(vm->clicks, 0);
}

TEST(Mvvm, HigherOrderCanvasWinsHitTest) {
    engine::ecs::World world;
    auto back = std::make_shared<ClickViewModel>();
    auto front = std::make_shared<ClickViewModel>();
    spawn_button_canvas(world, back, {0.0f, 0.0f, 100.0f, 100.0f}, 0);
    spawn_button_canvas(world, front, {0.0f, 0.0f, 100.0f, 100.0f}, 1);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 40.0f, 40.0f);

    EXPECT_EQ(front->clicks, 1);
    EXPECT_EQ(back->clicks, 0);
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
}

TEST(Mvvm, MouseConsumedResetOnBeginFrame) {
    engine::ecs::World world;
    world.ctx<engine::ui::MouseConsumed>().value = true;
    engine::ui::begin_frame(world);
    EXPECT_FALSE(world.ctx<engine::ui::MouseConsumed>().value);
}

TEST(Mvvm, FillWindowResizeWritesRect) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas = make_canvas({10.0f, 20.0f, 30.0f, 40.0f});
    canvas.fit = engine::ui::UiFit::FillWindow;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.ctx<engine::ui::WindowSize>().width = 800;
    world.ctx<engine::ui::WindowSize>().height = 600;
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

    world.ctx<engine::ui::WindowSize>().width = 800;
    world.ctx<engine::ui::WindowSize>().height = 600;
    engine::ui::begin_frame(world);

    EXPECT_EQ(world.get<engine::ui::UiCanvas>(entity).rect, original);
}

