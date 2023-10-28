#include <gtest/gtest.h>

#include <engine/core/input_system.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

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

TEST(Input, BindScancodeToAction) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    input.bind(engine::KeyCode{4}, "jump");
    input.handle_key(engine::KeyCode{4}, true);

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, "jump");
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
}

TEST(Input, UnboundKeyIgnored) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    input.handle_key(engine::KeyCode{4}, true);
    input.handle_key(engine::KeyCode{4}, false);

    EXPECT_TRUE(read_input(world).empty());
    EXPECT_FALSE(input.is_held("jump"));
}

TEST(Input, DownUpAndHeld) {
    engine::ecs::World world;
    engine::InputSystem input{world};
    input.bind(engine::KeyCode{4}, "jump");

    input.handle_key(engine::KeyCode{4}, true);
    EXPECT_TRUE(input.is_held("jump"));
    EXPECT_TRUE(input.is_held("jump"));

    input.handle_key(engine::KeyCode{4}, true);
    EXPECT_TRUE(input.is_held("jump"));

    input.handle_key(engine::KeyCode{4}, false);
    EXPECT_FALSE(input.is_held("jump"));

    const std::vector<engine::InputEvent> events = read_input(world);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].action, "jump");
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
    EXPECT_EQ(events[1].action, "jump");
    EXPECT_EQ(events[1].kind, engine::InputEvent::Kind::Up);
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
}
