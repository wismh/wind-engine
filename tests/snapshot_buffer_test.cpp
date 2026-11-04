#include <gtest/gtest.h>

#include "net/snapshot_buffer.h"

#include <engine/net/net_messages.h>

#include <vector>

namespace engine::net {
namespace {

SnapshotDelta at(Tick tick) {
    return SnapshotDelta{.tick = tick};
}

SnapshotRecord create(NetId id, std::uint8_t value) {
    return SnapshotRecord{.id = id, .owner = kServerPeerId, .changed = 1u, .blobs = {{value}}};
}

SnapshotRecord update(NetId id, std::uint8_t value) {
    return SnapshotRecord{.id = id, .changed = 1u, .blobs = {{value}}};
}

std::uint8_t value_of(const SnapshotBuffer& buffer, Tick tick, NetId id) {
    const SnapshotState* state = buffer.find(tick);
    EXPECT_NE(state, nullptr);
    const EntitySnapshot* entity = state != nullptr ? state->find(id) : nullptr;
    EXPECT_NE(entity, nullptr);
    return entity != nullptr ? entity->component(0)[0] : std::uint8_t{0};
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

TEST(SnapshotBuffer, EntitiesCarryForwardThroughSnapshotsThatOmitThem) {
    SnapshotBuffer buffer;
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 2, .records = {create(1, 10)}}));
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 4, .records = {create(2, 20)}}));
    EXPECT_EQ(value_of(buffer, 4, 1), 10);
    EXPECT_EQ(value_of(buffer, 4, 2), 20);
}

TEST(SnapshotBuffer, LateSnapshotReachesLaterStatesButNotPastNewerWrites) {
    SnapshotBuffer buffer;
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 2, .records = {create(1, 10), create(2, 20)}}));
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 8, .records = {update(1, 18)}}));
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 6, .records = {update(1, 16), update(2, 26)}}));

    EXPECT_EQ(value_of(buffer, 6, 1), 16);
    EXPECT_EQ(value_of(buffer, 6, 2), 26);
    EXPECT_EQ(value_of(buffer, 8, 1), 18); // newer write wins
    EXPECT_EQ(value_of(buffer, 8, 2), 26); // the late write arrives in the state after it
}

TEST(SnapshotBuffer, LateRemovalAndRecreateKeepTickOrder) {
    SnapshotBuffer buffer;
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 2, .records = {create(1, 10)}}));
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 8, .records = {create(1, 18)}}));
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 6, .removed = {1}}));

    EXPECT_EQ(buffer.find(6)->find(1), nullptr);
    EXPECT_EQ(value_of(buffer, 8, 1), 18);
}

TEST(SnapshotBuffer, RemovingAnUnknownIdIsNormal) {
    SnapshotBuffer buffer;
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 2, .removed = {99}}));
    EXPECT_TRUE(buffer.find(2)->entities.empty());
}

TEST(SnapshotBuffer, UpdateOfAnUnknownEntityIsRefused) {
    SnapshotBuffer buffer;
    EXPECT_FALSE(buffer.insert(SnapshotDelta{.tick = 2, .records = {update(1, 10)}}));
    EXPECT_TRUE(buffer.empty());
}

TEST(SnapshotBuffer, WhatLeftTheWindowLivesOnForLateSnapshots) {
    SnapshotBuffer buffer;
    ASSERT_TRUE(buffer.insert(SnapshotDelta{.tick = 2, .records = {create(1, 10)}}));
    ASSERT_TRUE(buffer.insert(at(100)));
    ASSERT_TRUE(buffer.insert(at(2 + kSnapshotWindowTicks)));
    EXPECT_EQ(buffer.find(2), nullptr); // 2 left the window
    EXPECT_EQ(value_of(buffer, 100, 1), 10);

    // A late snapshot older than everything held derives the rest again, from what left the window.
    ASSERT_TRUE(buffer.insert(at(50)));
    EXPECT_EQ(value_of(buffer, 50, 1), 10);
    EXPECT_EQ(value_of(buffer, 100, 1), 10);
    EXPECT_EQ(value_of(buffer, 2 + kSnapshotWindowTicks, 1), 10);
}

} // namespace
} // namespace engine::net
