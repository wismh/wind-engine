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
    world.flush_events();
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
    world.flush_events();
    writer.send(Msg{2});
    world.flush_events();

    std::vector<int> seen = read_values(events);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 2);

    world.flush_events();
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

    world.flush_events();
    world.flush_events();

    EXPECT_TRUE(read_values(registered).empty());

    const std::vector<int> orphan_seen = read_values(orphan);
    ASSERT_EQ(orphan_seen.size(), 1u);
    EXPECT_EQ(orphan_seen[0], 9);

    engine::ecs::EventWriter<Msg>{world}.send(Msg{4});
    const std::vector<int> after_send = read_values(registered);
    ASSERT_EQ(after_send.size(), 1u);
    EXPECT_EQ(after_send[0], 4);
}

TEST(Events, CursorReaderDoesNotReSeeEventAcrossFlush) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();
    engine::ecs::EventCursor<Msg> cursor;

    engine::ecs::EventWriter<Msg>{events}.send(Msg{1});

    std::vector<int> seen;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor}) {
        seen.push_back(event.value);
    }
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], 1);

    // No new send — a system that runs every frame must not see the same event a second time
    // just because flush_events() demoted it from current_ into previous_. Before the
    // EventCursor<T> fix, the ad-hoc EventReader<T> read the whole 2-generation buffer
    // unconditionally every construction, so this assertion would have failed (seen == {1}).
    world.flush_events();
    seen.clear();
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor}) {
        seen.push_back(event.value);
    }
    EXPECT_TRUE(seen.empty());
}

TEST(Events, FreshCursorSeesEventsAlreadyInBuffer) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();

    engine::ecs::EventWriter<Msg>{events}.send(Msg{1});
    world.flush_events();
    engine::ecs::EventWriter<Msg>{events}.send(Msg{2});

    // Constructed only now, after both sends — still catches up on history rather than starting
    // from "now".
    engine::ecs::EventCursor<Msg> cursor;
    std::vector<int> seen;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor}) {
        seen.push_back(event.value);
    }
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], 1);
    EXPECT_EQ(seen[1], 2);
}

TEST(Events, IndependentCursorsEachSeeEventOnce) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();
    engine::ecs::EventCursor<Msg> cursor_a;
    engine::ecs::EventCursor<Msg> cursor_b;

    engine::ecs::EventWriter<Msg>{events}.send(Msg{1});

    std::vector<int> seen_a;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor_a}) {
        seen_a.push_back(event.value);
    }
    ASSERT_EQ(seen_a.size(), 1u);
    EXPECT_EQ(seen_a[0], 1);

    world.flush_events();

    // cursor_b reads later (after a flush with no new sends) but, never having read before,
    // still sees the event exactly once — independent of cursor_a's earlier read.
    std::vector<int> seen_b;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor_b}) {
        seen_b.push_back(event.value);
    }
    ASSERT_EQ(seen_b.size(), 1u);
    EXPECT_EQ(seen_b[0], 1);

    // cursor_a does not re-see it.
    std::vector<int> seen_a_again;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor_a}) {
        seen_a_again.push_back(event.value);
    }
    EXPECT_TRUE(seen_a_again.empty());
}

TEST(Events, CursorAdvancedPastSeveralSendsThenOnlySeesNewOnes) {
    engine::ecs::World world;
    engine::ecs::Events<Msg>& events = world.ctx<engine::ecs::Events<Msg>>();
    engine::ecs::EventCursor<Msg> cursor;

    engine::ecs::EventWriter<Msg> writer{events};
    writer.send(Msg{1});
    writer.send(Msg{2});
    writer.send(Msg{3});

    std::vector<int> seen;
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor}) {
        seen.push_back(event.value);
    }
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], 1);
    EXPECT_EQ(seen[1], 2);
    EXPECT_EQ(seen[2], 3);

    writer.send(Msg{4});
    writer.send(Msg{5});

    seen.clear();
    for (const Msg& event : engine::ecs::EventReader<Msg>{events, cursor}) {
        seen.push_back(event.value);
    }
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], 4);
    EXPECT_EQ(seen[1], 5);
}
