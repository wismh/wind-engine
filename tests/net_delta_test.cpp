#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_client.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>
#include <engine/net/replication_registry.h>

#include <cstdint>
#include <span>
#include <vector>

namespace engine::net {
namespace {

using testing::NetHarness;
using testing::snapshots_sent;

struct Health {
    int value{0};
};

void register_health(ecs::World& world) {
    register_replicated<Health>(
            world, "Health", [](ReplicaWriter& writer, const Health& value) { writer.bytes().write_i32(value.value); },
            [](ReplicaReader& reader, Health& value) { return reader.bytes().read(value.value); });
}

[[nodiscard]] bool is_ack(std::span<const std::uint8_t> bytes) {
    return testing::message_id(bytes) == kSnapshotAckMessageId;
}

void move_with_tick(NetHarness& net, NetId id) {
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [id](ecs::World& world) {
        if (const auto entity = find_entity(world, id)) {
            world.get<Transform>(*entity).position.x = static_cast<float>(world.ctx<NetServer>().tick());
        }
    });
}

[[nodiscard]] float client_x(NetHarness& net, NetId id) {
    const auto entity = net.client_entity(id);
    EXPECT_TRUE(entity.has_value());
    return entity ? net.client.get<Transform>(*entity).position.x : -1.0f;
}

TEST(NetDelta, FullUntilAcked) {
    NetHarness net(9201);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    net.client_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) { return is_ack(bytes); });
    net.connect();
    net.step(20);

    const auto snapshots = snapshots_sent(net.server_link);
    ASSERT_FALSE(snapshots.empty());
    for (const auto& snapshot : snapshots) {
        EXPECT_EQ(snapshot.baseline, kNoTick) << "tick " << snapshot.tick;
        EXPECT_EQ(snapshot.entities, 1u);
    }
    EXPECT_FALSE(net.server.ctx<NetServer>().acked_tick(net.server.ctx<NetServer>().peers().at(0)).has_value());
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, DeltaAgainstAckedBaselineOmitsUnchanged) {
    NetHarness net(9202);
    for (int i = 0; i < 10; ++i) {
        net.spawn(Transform{.position = {static_cast<float>(i), 1.0f, 0.0f}});
    }
    const NetId mover = net.spawn();
    move_with_tick(net, mover);
    net.connect();
    net.step(30);

    const auto snapshots = snapshots_sent(net.server_link);
    ASSERT_GE(snapshots.size(), 3u);
    const auto& first = snapshots.front();
    const auto& last = snapshots.back();
    EXPECT_EQ(first.baseline, kNoTick);
    EXPECT_EQ(first.entities, 11u);
    // The ack of the previous snapshot reached the server before this one was taken.
    EXPECT_EQ(last.baseline, last.tick - 2);
    EXPECT_EQ(last.entities, 1u);
    EXPECT_EQ(last.removed, 0u);
    EXPECT_LT(last.size * 5, first.size);
    EXPECT_EQ(net.server.ctx<NetServer>().acked_tick(net.server.ctx<NetServer>().peers().at(0)), last.tick - 2);
    EXPECT_NEAR(client_x(net, mover), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, LostDeltasRecover) {
    NetHarness net(9203);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    net.server_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) {
        const auto snapshot = testing::parse_snapshot(bytes);
        return snapshot && snapshot->tick >= 20 && snapshot->tick <= 40;
    });
    net.connect();
    net.step(70);

    // The deltas after the gap are based on the last snapshot the client acknowledged before it.
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        if (snapshot.tick == 42) {
            EXPECT_EQ(snapshot.baseline, 18u);
        }
    }
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, StaleAckFallsBackToFull) {
    NetHarness net(9204);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    int acks = 0;
    net.client_link.set_drop([&acks](std::size_t, std::span<const std::uint8_t> bytes) {
        // Only the first ack gets through.
        return is_ack(bytes) && acks++ > 0;
    });
    net.connect();
    net.step(200);

    const auto snapshots = snapshots_sent(net.server_link);
    ASSERT_GE(snapshots.size(), 2u);
    const Tick acked = snapshots.front().tick;
    for (const auto& snapshot : snapshots) {
        if (snapshot.tick == acked) {
            EXPECT_EQ(snapshot.baseline, kNoTick);
        } else if (snapshot.tick < acked + kSnapshotWindowTicks) {
            EXPECT_EQ(snapshot.baseline, acked) << "tick " << snapshot.tick;
        } else {
            EXPECT_EQ(snapshot.baseline, kNoTick) << "tick " << snapshot.tick;
        }
    }
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, RemovedComponentAndEntityReachTheClient) {
    NetHarness net(9205);
    register_health(net.server);
    register_health(net.client);
    const NetId kept = net.spawn();
    const NetId gone = net.spawn();
    net.server.emplace<Health>(net.server_entity(kept), Health{3});
    net.connect();
    net.step(20);
    ASSERT_NE(net.client.try_get<Health>(*net.client_entity(kept)), nullptr);
    ASSERT_TRUE(net.client_entity(gone).has_value());

    net.server.remove<Health>(net.server_entity(kept));
    net.server.destroy(net.server_entity(gone));
    net.step(20);

    ASSERT_TRUE(net.client_entity(kept).has_value());
    EXPECT_EQ(net.client.try_get<Health>(*net.client_entity(kept)), nullptr);
    EXPECT_FALSE(net.client_entity(gone).has_value());

    bool listed_removed = false;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        listed_removed = listed_removed || (snapshot.baseline != kNoTick && snapshot.removed == 1u);
    }
    EXPECT_TRUE(listed_removed);
}

} // namespace
} // namespace engine::net
