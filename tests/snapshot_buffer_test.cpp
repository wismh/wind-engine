#include <gtest/gtest.h>

#include "net/snapshot_buffer.h"

#include <engine/net/net_messages.h>

#include <memory>
#include <vector>

namespace engine::net {
namespace {

std::shared_ptr<const SnapshotState> at(Tick tick) {
    return std::make_shared<const SnapshotState>(SnapshotState{.tick = tick});
}

TEST(SnapshotBuffer, InsertKeepsTickOrder) {
    SnapshotBuffer buffer;
    EXPECT_TRUE(buffer.insert(at(6)));
    EXPECT_TRUE(buffer.insert(at(2)));
    EXPECT_TRUE(buffer.insert(at(4)));
    EXPECT_EQ(buffer.ticks(), (std::vector<Tick>{2, 4, 6}));
    EXPECT_EQ(buffer.newest(), 6u);
    EXPECT_NE(buffer.find(4), nullptr);
    EXPECT_EQ(buffer.find(5), nullptr);
}

TEST(SnapshotBuffer, RejectsDuplicateAndOutsideWindow) {
    SnapshotBuffer buffer;
    EXPECT_TRUE(buffer.insert(at(200)));
    EXPECT_FALSE(buffer.insert(at(200)));
    EXPECT_FALSE(buffer.insert(at(kNoTick)));
    EXPECT_FALSE(buffer.insert(at(200 - kSnapshotWindowTicks)));
    EXPECT_TRUE(buffer.insert(at(200 - kSnapshotWindowTicks + 1)));
}

TEST(SnapshotBuffer, EvictsOutsideWindow) {
    SnapshotBuffer buffer;
    EXPECT_TRUE(buffer.insert(at(10)));
    EXPECT_TRUE(buffer.insert(at(20)));
    EXPECT_TRUE(buffer.insert(at(10 + kSnapshotWindowTicks)));
    EXPECT_EQ(buffer.ticks(), (std::vector<Tick>{20, 10 + kSnapshotWindowTicks}));
}

TEST(SnapshotBuffer, SampleBetweenHoldAndBefore) {
    SnapshotBuffer buffer;
    buffer.insert(at(10));
    buffer.insert(at(14));
    const SnapshotState* from = nullptr;
    const SnapshotState* to = nullptr;
    float t = -1.0f;

    EXPECT_FALSE(buffer.sample(9.5, from, to, t));

    ASSERT_TRUE(buffer.sample(11.0, from, to, t));
    EXPECT_EQ(from->tick, 10u);
    ASSERT_NE(to, nullptr);
    EXPECT_EQ(to->tick, 14u);
    EXPECT_FLOAT_EQ(t, 0.25f);

    ASSERT_TRUE(buffer.sample(14.0, from, to, t));
    EXPECT_EQ(from->tick, 14u);
    EXPECT_EQ(to, nullptr);

    ASSERT_TRUE(buffer.sample(30.0, from, to, t)); // holds the newest, no extrapolation
    EXPECT_EQ(from->tick, 14u);
    EXPECT_EQ(to, nullptr);
    EXPECT_FLOAT_EQ(t, 0.0f);
}

} // namespace
} // namespace engine::net
