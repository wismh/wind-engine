#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>
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
    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
}

TEST(Mvvm, ButtonClickSkippedWhenCannotExecute) {
    engine::ecs::World world;
    auto vm = std::make_shared<ClickViewModel>();
    vm->click.set_can_execute(false);
    spawn_button_canvas(world, vm, {0.0f, 0.0f, 100.0f, 100.0f}, 0);

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

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
    engine::ui::handle_pointer(world, 4.0f, 4.0f);

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

