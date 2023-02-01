#include <gtest/gtest.h>

#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

#include <vector>

namespace {

struct Msg {
    int value = 0;
};

struct Other {
    int value = 0;
};

struct NotAnEvent {
    int value = 0;
};

template<typename T>
std::vector<int> read_values(const engine::ecs::Events<T>& events) {
    std::vector<int> values;
    for (const T& event : engine::ecs::EventReader<T>{events}) {
        values.push_back(event.value);
    }
    return values;
}

}

TEST(Events, Send) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();
    engine::ecs::EventWriter<Msg> writer{events};
    writer.send(Msg{7});

    const std::vector<int> seen = read_values(events);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 7);
}

TEST(Events, ReaderSeesCurrentAndPrevious) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();

    engine::ecs::EventWriter<Msg>{events}.send(Msg{1});
    world.FlushEvents();
    engine::ecs::EventWriter<Msg>{events}.send(Msg{2});

    const std::vector<int> seen = read_values(events);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], 1);
    EXPECT_EQ(seen[1], 2);
}

TEST(Events, FlushEventsDropsOlderThanTwoFrames) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();
    engine::ecs::EventWriter<Msg> writer{world};

    writer.send(Msg{1});
    world.FlushEvents();
    writer.send(Msg{2});
    world.FlushEvents();

    std::vector<int> seen = read_values(events);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 2);

    world.FlushEvents();
    seen = read_values(events);
    EXPECT_TRUE(seen.empty());
}

TEST(Events, FirstCtxRegistersType) {
    engine::ecs::World world;
    world.ctx<NotAnEvent>();

    engine::ecs::Events<Msg>& registered = world.ctx<engine::ecs::Events<Msg>>();
    registered.send(Msg{3});

    engine::ecs::Events<Other> orphan;
    orphan.send(Other{9});

    world.FlushEvents();
    world.FlushEvents();

    EXPECT_TRUE(read_values(registered).empty());

    const std::vector<int> orphan_seen = read_values(orphan);
    ASSERT_EQ(orphan_seen.size(), 1u);
    EXPECT_EQ(orphan_seen[0], 9);

    engine::ecs::EventWriter<Msg>{world}.send(Msg{4});
    const std::vector<int> after_send = read_values(registered);
    ASSERT_EQ(after_send.size(), 1u);
    EXPECT_EQ(after_send[0], 4);
}
