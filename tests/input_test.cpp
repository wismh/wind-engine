#include <gtest/gtest.h>

#include <engine/core/input_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>

#include <vector>

namespace {

std::vector<engine::InputEvent> read_input(engine::ecs::World& world) {
    std::vector<engine::InputEvent> events;
    for (const engine::InputEvent& event : engine::ecs::EventReader<engine::InputEvent>{world}) {
        events.push_back(event);
    }
    return events;
}

std::vector<engine::MouseEvent> read_mouse(engine::ecs::World& world) {
    std::vector<engine::MouseEvent> events;
    for (const engine::MouseEvent& event : engine::ecs::EventReader<engine::MouseEvent>{world}) {
        events.push_back(event);
    }
    return events;
}

}

TEST(Input, InternSameNameTwiceEqual) {
    engine::InputSystem input;
    const engine::ActionId a = input.intern("jump");
    const engine::ActionId b = input.intern("jump");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, engine::ActionId::Invalid);
    EXPECT_EQ(input.debug_name(a), "jump");
}

TEST(Input, InternDifferentNamesUnequal) {
    engine::InputSystem input;
    EXPECT_NE(input.intern("jump"), input.intern("fire"));
    EXPECT_NE(input.intern("jump"), input.intern("Jump"));
}

TEST(Input, EmptyAndWhitespaceNameInvalid) {
    engine::InputSystem input;
    EXPECT_EQ(input.intern(""), engine::ActionId::Invalid);
    EXPECT_EQ(input.intern("   "), engine::ActionId::Invalid);
    EXPECT_EQ(input.intern("\t\n"), engine::ActionId::Invalid);
    EXPECT_FALSE(input.find(""));
    EXPECT_FALSE(input.find("   "));
    EXPECT_TRUE(input.debug_name(engine::ActionId::Invalid).empty());
}

TEST(Input, FindMissingIsEmpty) {
    engine::InputSystem input;
    EXPECT_FALSE(input.find("jump"));
    const engine::ActionId jump = input.intern("jump");
    const std::optional<engine::ActionId> found = input.find("jump");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, jump);
    EXPECT_FALSE(input.find("fire"));
}

TEST(Input, KeyCodeAEqualsFour) {
    EXPECT_EQ(engine::KeyCode::A, engine::KeyCode{4});
}

TEST(Input, KeyCodeSpaceEqualsFortyFour) {
    EXPECT_EQ(engine::KeyCode::Space, engine::KeyCode{44});
}

TEST(Input, BindScancodeToAction) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    input.bind(engine::KeyCode::A, jump);
    input.handle_key(engine::KeyCode::A, true);

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, jump);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FLOAT_EQ(events[0].value, 1.f);
}

TEST(Input, UnboundKeyIgnored) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    input.handle_key(engine::KeyCode::A, true);
    input.handle_key(engine::KeyCode::A, false);

    EXPECT_TRUE(read_input(world).empty());
    EXPECT_FALSE(input.is_held(jump));
}

TEST(Input, DownUpAndHeld) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    input.bind(engine::KeyCode::A, jump);

    input.handle_key(engine::KeyCode::A, true);
    EXPECT_TRUE(input.is_held(jump));
    EXPECT_TRUE(input.is_held(jump));

    input.handle_key(engine::KeyCode::A, true);
    EXPECT_TRUE(input.is_held(jump));

    input.handle_key(engine::KeyCode::A, false);
    EXPECT_FALSE(input.is_held(jump));

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].action, jump);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FLOAT_EQ(events[0].value, 1.f);
    EXPECT_EQ(events[1].action, jump);
    EXPECT_EQ(events[1].kind, engine::InputEvent::Kind::Up);
    EXPECT_FLOAT_EQ(events[1].value, 0.f);
}

TEST(Input, BindKeyNameMatchesIntern) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    input.bind(engine::KeyCode::A, "jump");
    input.handle_key(engine::KeyCode::A, true);

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, jump);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FLOAT_EQ(events[0].value, 1.f);
    EXPECT_TRUE(input.is_held(jump));
}

TEST(Input, RebindSpaceFromJumpToFire) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::KeyCode::Space, jump);
    EXPECT_EQ(input.bound_action(engine::KeyCode::Space), jump);

    input.bind(engine::KeyCode::Space, fire);
    EXPECT_EQ(input.bound_action(engine::KeyCode::Space), fire);

    input.handle_key(engine::KeyCode::Space, true);
    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, fire);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FALSE(input.is_held(jump));
    EXPECT_TRUE(input.is_held(fire));
}

TEST(Input, UnbindWhileHeldSendsUp) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    input.bind(engine::KeyCode::A, jump);
    input.handle_key(engine::KeyCode::A, true);
    EXPECT_TRUE(input.is_held(jump));

    input.unbind(engine::KeyCode::A);
    EXPECT_FALSE(input.is_held(jump));
    EXPECT_EQ(input.bound_action(engine::KeyCode::A), engine::ActionId::Invalid);

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].action, jump);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_EQ(events[1].action, jump);
    EXPECT_EQ(events[1].kind, engine::InputEvent::Kind::Up);
    EXPECT_FLOAT_EQ(events[1].value, 0.f);
}

TEST(Input, ControlsForStableOrder) {
    engine::InputSystem input;
    const engine::ActionId jump = input.intern("jump");
    input.bind(engine::KeyCode::Space, jump);
    input.bind(engine::KeyCode::A, jump);

    const std::vector<engine::Control> controls = input.controls_for(jump);
    ASSERT_EQ(controls.size(), 2u);
    EXPECT_EQ(controls[0].kind, engine::ControlKind::Key);
    EXPECT_EQ(controls[0].code, static_cast<std::uint32_t>(engine::KeyCode::A));
    EXPECT_EQ(controls[0].device, 0u);
    EXPECT_EQ(controls[1].kind, engine::ControlKind::Key);
    EXPECT_EQ(controls[1].code, static_cast<std::uint32_t>(engine::KeyCode::Space));
    EXPECT_EQ(controls[1].device, 0u);
}

TEST(Input, RebindHeldKeyReleasesOldAction) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId jump = input.intern("jump");
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::KeyCode::Space, jump);
    input.handle_key(engine::KeyCode::Space, true);
    EXPECT_TRUE(input.is_held(jump));

    input.bind(engine::KeyCode::Space, fire);
    EXPECT_FALSE(input.is_held(jump));
    EXPECT_FALSE(input.is_held(fire));
    EXPECT_EQ(input.bound_action(engine::KeyCode::Space), fire);

    input.handle_key(engine::KeyCode::Space, true);
    EXPECT_TRUE(input.is_held(fire));

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].action, jump);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_EQ(events[1].action, jump);
    EXPECT_EQ(events[1].kind, engine::InputEvent::Kind::Up);
    EXPECT_FLOAT_EQ(events[1].value, 0.f);
    EXPECT_EQ(events[2].action, fire);
    EXPECT_EQ(events[2].kind, engine::InputEvent::Kind::Down);
}

TEST(Input, MouseDownMoveUp) {
    engine::ecs::World world;
    engine::InputSystem input{world};

    const glm::vec2 down_pos{12.f, 34.f};
    const glm::vec2 move_pos{40.f, 50.f};
    const glm::vec2 relative{1.5f, -2.f};
    const glm::vec2 up_pos{41.f, 48.f};

    input.handle_mouse_button(engine::MouseButton::Left, true, down_pos);
    input.handle_mouse_move(move_pos, relative);
    input.handle_mouse_button(engine::MouseButton::Left, false, up_pos);

    const std::vector<engine::MouseEvent> events = read_mouse(world);
    ASSERT_EQ(events.size(), 3u);

    EXPECT_EQ(events[0].kind, engine::MouseEvent::Kind::Down);
    EXPECT_EQ(events[0].button, engine::MouseButton::Left);
    EXPECT_EQ(events[0].position, down_pos);

    EXPECT_EQ(events[1].kind, engine::MouseEvent::Kind::Move);
    EXPECT_EQ(events[1].position, move_pos);
    EXPECT_EQ(events[1].relative, relative);
    EXPECT_EQ(events[1].button, engine::MouseButton::None);

    EXPECT_EQ(events[2].kind, engine::MouseEvent::Kind::Up);
    EXPECT_EQ(events[2].button, engine::MouseButton::Left);
    EXPECT_EQ(events[2].position, up_pos);

    EXPECT_TRUE(read_input(world).empty());
}

TEST(Input, BoundLeftMouseEmitsMouseAndInput) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::MouseButton::Left, fire);

    const glm::vec2 pos{8.f, 16.f};
    input.handle_mouse_button(engine::MouseButton::Left, true, pos);

    const std::vector<engine::MouseEvent> mouse = read_mouse(world);
    ASSERT_EQ(mouse.size(), 1u);
    EXPECT_EQ(mouse[0].kind, engine::MouseEvent::Kind::Down);
    EXPECT_EQ(mouse[0].button, engine::MouseButton::Left);
    EXPECT_EQ(mouse[0].position, pos);

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, fire);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FLOAT_EQ(events[0].value, 1.f);
    EXPECT_TRUE(input.is_held(fire));
}

TEST(Input, BoundLeftMouseHeldWhileDown) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::MouseButton::Left, "fire");
    EXPECT_EQ(input.bound_action(engine::MouseButton::Left), fire);

    input.handle_mouse_button(engine::MouseButton::Left, true, {1.f, 2.f});
    EXPECT_TRUE(input.is_held(fire));

    input.handle_mouse_button(engine::MouseButton::Left, false, {1.f, 2.f});
    EXPECT_FALSE(input.is_held(fire));
}

TEST(Input, KeyAndLeftMouseShareHeldCount) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::KeyCode::Space, fire);
    input.bind(engine::MouseButton::Left, fire);

    input.handle_key(engine::KeyCode::Space, true);
    input.handle_mouse_button(engine::MouseButton::Left, true, {10.f, 20.f});
    EXPECT_TRUE(input.is_held(fire));

    input.handle_key(engine::KeyCode::Space, false);
    EXPECT_TRUE(input.is_held(fire));

    input.handle_mouse_button(engine::MouseButton::Left, false, {10.f, 20.f});
    EXPECT_FALSE(input.is_held(fire));
}

TEST(Input, InputEventNotFilteredByMouseConsumed) {
    // InputSystem polls before UiInputSystem. It must not drop InputEvent when
    // MouseConsumed is already true. Phase::Game gameplay checks
    // world.ctx<ui::MouseConsumed>().value before treating Fire as a world action.
    engine::ecs::World world;
    world.ctx<engine::ui::MouseConsumed>().value = true;
    engine::InputSystem input{world};
    const engine::ActionId fire = input.intern("fire");
    input.bind(engine::MouseButton::Left, fire);
    input.handle_mouse_button(engine::MouseButton::Left, true, {0.f, 0.f});

    EXPECT_TRUE(world.ctx<engine::ui::MouseConsumed>().value);
    ASSERT_EQ(read_mouse(world).size(), 1u);
    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, fire);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_FLOAT_EQ(events[0].value, 1.f);
}

TEST(Input, DenormalizeTouchUsesDrawablePixels) {
    const glm::vec2 pos = engine::denormalize_touch({0.25f, 0.5f}, {800, 600});
    EXPECT_FLOAT_EQ(pos.x, 200.f);
    EXPECT_FLOAT_EQ(pos.y, 300.f);
}

TEST(Input, DenormalizeTouchZeroDrawable) {
    const glm::vec2 pos = engine::denormalize_touch({1.f, 1.f}, {0, 0});
    EXPECT_FLOAT_EQ(pos.x, 0.f);
    EXPECT_FLOAT_EQ(pos.y, 0.f);
}

TEST(Input, TouchPrimaryMapsToLeftMouse) {
    engine::ecs::World world;
    engine::InputSystem input{world};

    const glm::vec2 down_pos = engine::denormalize_touch({0.1f, 0.2f}, {100, 100});
    const glm::vec2 move_pos = engine::denormalize_touch({0.3f, 0.4f}, {100, 100});
    const glm::vec2 rel{2.f, 3.f};
    const glm::vec2 up_pos = engine::denormalize_touch({0.3f, 0.5f}, {100, 100});

    input.handle_touch(7, true, down_pos);
    ASSERT_TRUE(input.primary_touch_finger().has_value());
    EXPECT_EQ(*input.primary_touch_finger(), 7u);

    input.handle_touch_move(7, move_pos, rel);
    input.handle_touch(7, false, up_pos);
    EXPECT_FALSE(input.primary_touch_finger().has_value());

    const std::vector<engine::MouseEvent> events = read_mouse(world);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].kind, engine::MouseEvent::Kind::Down);
    EXPECT_EQ(events[0].button, engine::MouseButton::Left);
    EXPECT_EQ(events[0].position, down_pos);
    EXPECT_EQ(events[1].kind, engine::MouseEvent::Kind::Move);
    EXPECT_EQ(events[1].position, move_pos);
    EXPECT_EQ(events[1].relative, rel);
    EXPECT_EQ(events[2].kind, engine::MouseEvent::Kind::Up);
    EXPECT_EQ(events[2].button, engine::MouseButton::Left);
    EXPECT_EQ(events[2].position, up_pos);
}

TEST(Input, ExtraFingerDoesNotEmitMouse) {
    engine::ecs::World world;
    engine::InputSystem input{world};

    input.handle_touch(1, true, {10.f, 10.f});
    input.handle_touch(2, true, {20.f, 20.f});
    input.handle_touch_move(2, {21.f, 22.f}, {1.f, 2.f});
    input.handle_touch(2, false, {21.f, 22.f});

    ASSERT_TRUE(input.primary_touch_finger().has_value());
    EXPECT_EQ(*input.primary_touch_finger(), 1u);

    const std::vector<engine::MouseEvent> events = read_mouse(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, engine::MouseEvent::Kind::Down);
    EXPECT_EQ(events[0].position, (glm::vec2{10.f, 10.f}));
    EXPECT_TRUE(read_input(world).empty());
}

TEST(Input, ExtraFingerDoesNotStealPrimaryOnRelease) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    input.handle_touch(1, true, {1.f, 1.f});
    input.handle_touch(2, true, {2.f, 2.f});
    input.handle_touch(2, false, {2.f, 2.f});
    ASSERT_TRUE(input.primary_touch_finger().has_value());
    EXPECT_EQ(*input.primary_touch_finger(), 1u);
    input.handle_touch_move(1, {3.f, 4.f}, {0.f, 0.f});

    const std::vector<engine::MouseEvent> events = read_mouse(world);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].kind, engine::MouseEvent::Kind::Move);
    EXPECT_EQ(events[1].position, (glm::vec2{3.f, 4.f}));
}

TEST(Input, BoundTouchControlEmitsInputEvent) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId tap = input.intern("tap");
    const engine::Control finger{engine::ControlKind::Touch, 4, 0};
    input.bind(finger, tap);

    input.handle_touch(4, true, {8.f, 9.f});
    EXPECT_TRUE(input.is_held(tap));
    input.handle_touch(4, false, {8.f, 9.f});
    EXPECT_FALSE(input.is_held(tap));

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].action, tap);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_EQ(events[1].action, tap);
    EXPECT_EQ(events[1].kind, engine::InputEvent::Kind::Up);
}

TEST(Input, UnboundExtraFingerIgnoredForActions) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    const engine::ActionId tap = input.intern("tap");
    input.handle_touch(9, true, {0.f, 0.f});
    input.handle_touch(9, false, {0.f, 0.f});
    EXPECT_FALSE(input.is_held(tap));
    EXPECT_TRUE(read_input(world).empty());
}
