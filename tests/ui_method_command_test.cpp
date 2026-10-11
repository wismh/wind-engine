#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace {

class Panel {
public:
    int calls = 0;
    bool allowed = true;

    void poke() { ++calls; }
    [[nodiscard]] bool may_poke() const { return allowed; }
};

class OtherPanel {
public:
    int calls = 0;

    void poke() { calls += 10; }
};

class ButtonViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::MethodCommand click;

    ButtonViewModel() { command(engine::ui::intern("click"), click); }
};

}

TEST(MethodCommand, UnboundDoesNothingAndCannotExecute) {
    engine::ui::MethodCommand command;
    EXPECT_FALSE(command.can_execute());
    command.execute();
}

TEST(MethodCommand, ExecuteCallsTheBoundMemberOfTheBoundOwner) {
    Panel panel;
    engine::ui::MethodCommand command;
    command.bind_to<Panel, &Panel::poke>(panel);

    EXPECT_TRUE(command.can_execute());
    command.execute();
    command.execute();
    EXPECT_EQ(panel.calls, 2);
}

TEST(MethodCommand, TwoOwnersOfOneClassAreIndependent) {
    Panel first;
    Panel second;
    engine::ui::MethodCommand a;
    engine::ui::MethodCommand b;
    a.bind_to<Panel, &Panel::poke>(first);
    b.bind_to<Panel, &Panel::poke>(second);

    a.execute();
    b.execute();
    b.execute();
    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(second.calls, 2);
}

TEST(MethodCommand, RebindingMovesTheCommandToTheNewOwner) {
    Panel first;
    OtherPanel second;
    engine::ui::MethodCommand command;
    command.bind_to<Panel, &Panel::poke>(first);
    command.execute();

    command.bind_to<OtherPanel, &OtherPanel::poke>(second);
    command.execute();

    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(second.calls, 10);
}

TEST(MethodCommand, CanExecuteMemberDecidesWhenBound) {
    Panel panel;
    engine::ui::MethodCommand command;
    command.bind_to<Panel, &Panel::poke, &Panel::may_poke>(panel);

    EXPECT_TRUE(command.can_execute());
    panel.allowed = false;
    EXPECT_FALSE(command.can_execute());
    command.execute();
    EXPECT_EQ(panel.calls, 0);

    panel.allowed = true;
    command.execute();
    EXPECT_EQ(panel.calls, 1);
}

TEST(MethodCommand, RebindingWithoutCanExecuteDropsThePreviousOne) {
    Panel panel;
    panel.allowed = false;
    engine::ui::MethodCommand command;
    command.bind_to<Panel, &Panel::poke, &Panel::may_poke>(panel);
    EXPECT_FALSE(command.can_execute());

    command.bind_to<Panel, &Panel::poke>(panel);
    EXPECT_TRUE(command.can_execute());
}

TEST(MethodCommand, ButtonBoundByViewModelCallsTheOwner) {
    engine::ecs::World world;
    Panel panel;
    auto vm = std::make_shared<ButtonViewModel>();
    vm->click.bind_to<Panel, &Panel::poke>(panel);

    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);
    EXPECT_EQ(panel.calls, 1);
}

TEST(MethodCommand, UnboundButtonClickIsSwallowed) {
    engine::ecs::World world;
    auto vm = std::make_shared<ButtonViewModel>();

    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding click}" content="Go"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 4.0f, 4.0f);
    EXPECT_FALSE(vm->click.can_execute());
}
