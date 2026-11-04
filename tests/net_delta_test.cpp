#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_client.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cstdint>
#include <set>
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

TEST(NetDelta, EveryEntityIsRewrittenWholeUntilAcked) {
    NetHarness net(9201);
    const NetId id = net.spawn();
    const NetId still = net.spawn();
    move_with_tick(net, id);
    net.client_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) { return is_ack(bytes); });
    net.connect();
    net.step(20);

    const auto snapshots = snapshots_sent(net.server_link);
    ASSERT_FALSE(snapshots.empty());
    for (const auto& snapshot : snapshots) {
        // Nothing is known to have arrived, so the entity that does not move is written every time too.
        EXPECT_EQ(snapshot.entities, 2u) << "tick " << snapshot.tick;
    }
    EXPECT_FALSE(net.server.ctx<NetServer>().acked_tick(net.server.ctx<NetServer>().peers().at(0)).has_value());
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
    EXPECT_TRUE(net.client_entity(still).has_value());
}

TEST(NetDelta, AckedEntitiesAreNotRewritten) {
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
    EXPECT_EQ(first.entities, 11u);
    EXPECT_EQ(last.entities, 1u);
    EXPECT_EQ(last.ids, std::vector<NetId>{mover});
    EXPECT_EQ(last.removed, 0u);
    EXPECT_LT(last.size * 5, first.size);
    // The ack of the previous snapshot reached the server before this one was taken.
    EXPECT_EQ(net.server.ctx<NetServer>().acked_tick(net.server.ctx<NetServer>().peers().at(0)), last.tick - 2);
    EXPECT_NEAR(client_x(net, mover), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, LostSnapshotsRecover) {
    NetHarness net(9203);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    net.server_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) {
        const auto snapshot = testing::parse_snapshot(bytes);
        return snapshot && snapshot->tick >= 20 && snapshot->tick <= 40;
    });
    net.connect();
    net.step(70);
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
}

TEST(NetDelta, LostAcksOnlyCostRewrites) {
    NetHarness net(9204);
    const NetId id = net.spawn();
    const NetId still = net.spawn(Transform{.position = {3.0f, 0.0f, 0.0f}});
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
    // The first ack confirmed the entity that does not move; the other one keeps being written, past the window too.
    for (std::size_t i = 2; i < snapshots.size(); ++i) {
        EXPECT_EQ(snapshots[i].ids, std::vector<NetId>{id}) << "tick " << snapshots[i].tick;
    }
    EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3);
    EXPECT_FLOAT_EQ(client_x(net, still), 3.0f);
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
        listed_removed = listed_removed || snapshot.removed_ids == std::vector<NetId>{gone};
    }
    EXPECT_TRUE(listed_removed);
}

TEST(NetDelta, RemovalIsListedUntilAcked) {
    NetHarness net(9206);
    const NetId gone = net.spawn();
    net.connect();
    net.step(10);
    net.server_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) {
        const auto snapshot = testing::parse_snapshot(bytes);
        return snapshot && snapshot->removed > 0 && snapshot->tick <= 20;
    });
    net.server.destroy(net.server_entity(gone));
    net.step(30);

    EXPECT_FALSE(net.client_entity(gone).has_value());
    const auto snapshots = snapshots_sent(net.server_link);
    EXPECT_GT(std::count_if(snapshots.begin(), snapshots.end(), [](const auto& s) { return s.removed > 0; }), 2);
    EXPECT_EQ(snapshots.back().removed, 0u);
}

TEST(NetDelta, ValueThatCameBackWhileAnotherWasInFlightReachesTheClient) {
    NetHarness net(9207);
    register_health(net.server);
    register_health(net.client);
    const NetId id = net.spawn();
    net.server.emplace<Health>(net.server_entity(id), Health{1});
    net.connect();
    net.step(20);
    ASSERT_EQ(net.client.get<Health>(*net.client_entity(id)).value, 1);

    // The client gets the 2, but its acks go missing while the server goes back to 1.
    net.client_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) { return is_ack(bytes); });
    net.server.get<Health>(net.server_entity(id)).value = 2;
    net.step(20);
    ASSERT_EQ(net.client.get<Health>(*net.client_entity(id)).value, 2);

    net.server_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) {
        return testing::parse_snapshot(bytes).has_value();
    });
    net.server.get<Health>(net.server_entity(id)).value = 1;
    net.step(6);
    // The 1 equals what the client last acknowledged, and went nowhere. Only the unconfirmed 2 says it is not done.
    ASSERT_EQ(net.client.get<Health>(*net.client_entity(id)).value, 2);
    net.server_link.set_drop({});
    net.client_link.set_drop({});
    net.step(20);
    EXPECT_EQ(net.client.get<Health>(*net.client_entity(id)).value, 1);
    // And once acknowledged it is no longer written.
    EXPECT_EQ(snapshots_sent(net.server_link).back().entities, 0u);
}

TEST(NetDelta, PeersAckIndependently) {
    NetHarness net(9208);
    auto& second = net.add_client();
    const NetId id = net.spawn();
    second.link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) { return is_ack(bytes); });
    net.connect();
    net.step(20);

    const auto peers = net.server.ctx<NetServer>().peers();
    ASSERT_EQ(peers.size(), 2u);
    EXPECT_TRUE(net.server.ctx<NetServer>().acked_tick(peers[0]).has_value());
    EXPECT_FALSE(net.server.ctx<NetServer>().acked_tick(peers[1]).has_value());

    // The entity is clean for the first peer and still written to the second.
    std::vector<testing::WireSnapshot> first_peer;
    std::vector<testing::WireSnapshot> second_peer;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        (snapshot.target == peers[0] ? first_peer : second_peer).push_back(snapshot);
    }
    EXPECT_EQ(first_peer.back().entities, 0u);
    EXPECT_EQ(second_peer.back().ids, std::vector<NetId>{id});
    EXPECT_TRUE(second.entity(id).has_value());
}

} // namespace
} // namespace engine::net
