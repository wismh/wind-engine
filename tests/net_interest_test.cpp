#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_always_relevant.h>
#include <engine/net/net_client.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>
#include <engine/net/prediction.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::net {
namespace {

using testing::NetClientSide;
using testing::NetHarness;

struct Health {
    int value{0};
};

void register_health(ecs::World& world) {
    register_replicated<Health>(
            world, "Health", [](ReplicaWriter& writer, const Health& value) { writer.bytes().write_i32(value.value); },
            [](ReplicaReader& reader, Health& value) { return reader.bytes().read(value.value); });
}

// Cells of 10 units, 2 cells of radius, a margin of 1.
NetServerConfig grid_config() {
    NetServerConfig config;
    config.interest.cell_size = 10.0f;
    config.interest.radius_cells = 2;
    config.interest.leave_margin_cells = 1;
    return config;
}

Transform at(float x) {
    return Transform{.position = {x, 0.0f, 0.0f}};
}

void place(NetHarness& net, NetId id, float x) {
    net.server.get<Transform>(net.server_entity(id)).position.x = x;
}

PeerId peer_of(NetHarness& net, std::size_t index) {
    return net.server.ctx<NetServer>().peers().at(index);
}

TEST(NetInterest, NoInterestReplicatesEverything) {
    NetHarness net(9301);
    const NetId near = net.spawn(at(0.0f));
    const NetId far = net.spawn(at(100000.0f));
    net.connect();
    net.step(20);
    EXPECT_TRUE(net.client_entity(near).has_value());
    EXPECT_TRUE(net.client_entity(far).has_value());
    EXPECT_EQ(net.server.ctx<NetServer>().peer_stats(peer_of(net, 0))->relevant, 2u);
}

TEST(NetInterest, GridEnterAndLeaveSpawnsAndDespawnsOnThatPeerOnly) {
    NetHarness net(9302, grid_config());
    NetClientSide& other = net.add_client();
    const NetId mob = net.spawn(at(0.0f));
    net.connect();
    net.step(2);

    // The first client looks from x = 500; the other from x = 0, where the mob starts.
    const glm::vec3 first_focus{500.0f, 0.0f, 0.0f};
    const glm::vec3 other_focus{0.0f, 0.0f, 0.0f};
    auto& server = net.server.ctx<NetServer>();
    server.set_peer_focus(peer_of(net, 0), std::span(&first_focus, 1));
    server.set_peer_focus(peer_of(net, 1), std::span(&other_focus, 1));
    net.step(20);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    EXPECT_TRUE(other.entity(mob).has_value());
    EXPECT_TRUE(net.spawned.empty());

    // In range of the first client, out of the other's. Both on the render timeline: not at once.
    place(net, mob, 505.0f);
    net.step(1);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    EXPECT_TRUE(other.entity(mob).has_value());
    net.step(20);
    EXPECT_TRUE(net.client_entity(mob).has_value());
    EXPECT_FALSE(other.entity(mob).has_value());
    ASSERT_EQ(net.spawned.size(), 1u);
    EXPECT_EQ(net.spawned[0].net_id, mob);
    ASSERT_EQ(other.despawned.size(), 1u);
    EXPECT_EQ(other.despawned[0].net_id, mob);

    // And out again, for the first client.
    place(net, mob, 0.0f);
    net.step(20);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    EXPECT_TRUE(other.entity(mob).has_value());
    EXPECT_EQ(net.despawned.size(), 1u);
    EXPECT_EQ(other.spawned.size(), 2u);
}

TEST(NetInterest, LeaveMarginStopsFlapping) {
    NetHarness net(9303, grid_config());
    const NetId mob = net.spawn(at(5.0f));
    net.connect();
    net.step(2);
    const glm::vec3 focus{5.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(10);
    ASSERT_TRUE(net.client_entity(mob).has_value());

    // Back and forth over the edge of the radius (cell 2 is in, cell 3 is the margin, cell 4 is out).
    for (int i = 0; i < 40; ++i) {
        place(net, mob, i % 2 == 0 ? 25.0f : 35.0f);
        net.step(2);
    }
    EXPECT_TRUE(net.client_entity(mob).has_value());
    EXPECT_EQ(net.spawned.size(), 1u);
    EXPECT_TRUE(net.despawned.empty());

    place(net, mob, 45.0f);
    net.step(20);
    EXPECT_FALSE(net.client_entity(mob).has_value());

    // From outside, the margin does not let it in.
    place(net, mob, 35.0f);
    net.step(20);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    place(net, mob, 25.0f);
    net.step(20);
    EXPECT_TRUE(net.client_entity(mob).has_value());
}

TEST(NetInterest, OwnedEntityIsAlwaysRelevantAndAFocus) {
    NetServerConfig config = grid_config();
    config.interest.relevant = [](const ecs::World&, ecs::Entity, PeerId) { return false; };
    NetHarness net(9304, config);
    net.connect();
    net.step(2);
    const ecs::Entity entity = net.server.create();
    net.server.emplace<Transform>(entity, at(9000.0f));
    const NetId mine = replicate(net.server, entity, peer_of(net, 0));
    net.step(20);
    ASSERT_TRUE(net.client_entity(mine).has_value());
    EXPECT_EQ(net.client.get<NetIdentity>(*net.client_entity(mine)).owner, peer_of(net, 0));
}

TEST(NetInterest, OwnedEntityMovesTheFocus) {
    NetHarness net(9305, grid_config());
    net.connect();
    net.step(2);
    const ecs::Entity player = net.server.create();
    net.server.emplace<Transform>(player, at(0.0f));
    (void)replicate(net.server, player, peer_of(net, 0));
    const NetId near = net.spawn(at(10.0f));
    const NetId far = net.spawn(at(200.0f));
    net.step(20);
    EXPECT_TRUE(net.client_entity(near).has_value());
    EXPECT_FALSE(net.client_entity(far).has_value());

    net.server.get<Transform>(player).position.x = 200.0f;
    net.step(20);
    EXPECT_FALSE(net.client_entity(near).has_value());
    EXPECT_TRUE(net.client_entity(far).has_value());
}

TEST(NetInterest, RelevantFilterHidesFromOnePeerAndRunsOnlyOnCandidates) {
    NetServerConfig config = grid_config();
    PeerId hidden_from = kInvalidPeerId;
    int calls = 0;
    NetId hidden_id = kInvalidNetId;
    config.interest.relevant = [&](const ecs::World& world, ecs::Entity entity, PeerId peer) {
        ++calls;
        return !(peer == hidden_from && world.get<NetIdentity>(entity).id == hidden_id);
    };
    NetHarness net(9306, config);
    NetClientSide& other = net.add_client();
    hidden_id = net.spawn(at(0.0f));
    const NetId shown = net.spawn(at(5.0f));
    const NetId out_of_range = net.spawn(at(9000.0f));
    net.connect();
    net.step(2);
    hidden_from = peer_of(net, 0);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    auto& server = net.server.ctx<NetServer>();
    server.set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    server.set_peer_focus(peer_of(net, 1), std::span(&focus, 1));
    calls = 0;
    net.step(20);
    EXPECT_FALSE(net.client_entity(hidden_id).has_value());
    EXPECT_TRUE(other.entity(hidden_id).has_value());
    EXPECT_TRUE(net.client_entity(shown).has_value());
    EXPECT_FALSE(net.client_entity(out_of_range).has_value());
    // 10 sends, 2 peers, 2 candidates in range.
    EXPECT_EQ(calls, 10 * 2 * 2);
}

TEST(NetInterest, FilterWithoutAGridDecidesAlone) {
    NetServerConfig config;
    config.interest.relevant = [](const ecs::World& world, ecs::Entity entity, PeerId) {
        return world.get<Transform>(entity).position.x < 100.0f;
    };
    NetHarness net(9307, config);
    const NetId kept = net.spawn(at(0.0f));
    const NetId dropped = net.spawn(at(100.0f));
    net.connect();
    net.step(20);
    EXPECT_TRUE(net.client_entity(kept).has_value());
    EXPECT_FALSE(net.client_entity(dropped).has_value());
}

TEST(NetInterest, AlwaysRelevantAndNonSpatialIgnoreTheGrid) {
    NetHarness net(9308, grid_config());
    const NetId tagged = net.spawn(at(9000.0f));
    net.server.emplace<NetAlwaysRelevant>(net.server_entity(tagged));
    const ecs::Entity match_state = net.server.create();
    const NetId non_spatial = replicate(net.server, match_state);
    const NetId far = net.spawn(at(9000.0f));
    net.connect();
    net.step(20);
    EXPECT_TRUE(net.client_entity(tagged).has_value());
    EXPECT_TRUE(net.client_entity(non_spatial).has_value());
    EXPECT_FALSE(net.client_entity(far).has_value());
}

TEST(NetInterest, ChildUsesItsRootsCellAndComesWithItsAncestors) {
    NetHarness net(9309, grid_config());
    const NetId root = net.spawn(at(1000.0f));
    const NetId middle = net.spawn(at(0.0f));
    const NetId child = net.spawn(at(0.0f)); // local x 0 would be in range of the focus if it counted
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(middle), net.server_entity(root)));
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(child), net.server_entity(middle)));
    net.connect();
    net.step(2);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(20);
    EXPECT_FALSE(net.client_entity(child).has_value());

    place(net, root, 5.0f);
    net.step(20);
    ASSERT_TRUE(net.client_entity(root).has_value());
    ASSERT_TRUE(net.client_entity(middle).has_value());
    ASSERT_TRUE(net.client_entity(child).has_value());
    EXPECT_EQ(net.client.get<Parent>(*net.client_entity(child)).entity, *net.client_entity(middle));
}

TEST(NetInterest, OwnedChildPullsInFarAncestors) {
    NetServerConfig config = grid_config();
    config.interest.relevant = [](const ecs::World&, ecs::Entity, PeerId) { return false; };
    NetHarness net(9310, config);
    const NetId parent = net.spawn(at(9000.0f));
    net.connect();
    net.step(2);
    const ecs::Entity child = net.server.create();
    net.server.emplace<Transform>(child, at(0.0f));
    const NetId owned = replicate(net.server, child, peer_of(net, 0));
    ASSERT_TRUE(ecs::set_parent(net.server, child, net.server_entity(parent)));
    net.step(20);
    ASSERT_TRUE(net.client_entity(owned).has_value());
    ASSERT_TRUE(net.client_entity(parent).has_value());
    EXPECT_EQ(net.client.get<Parent>(*net.client_entity(owned)).entity, *net.client_entity(parent));
}

TEST(NetInterest, FilteredChildStaysOutOfARelevantParent) {
    NetServerConfig config = grid_config();
    NetId hidden_child = kInvalidNetId;
    config.interest.relevant = [&hidden_child](const ecs::World& world, ecs::Entity entity, PeerId) {
        return world.get<NetIdentity>(entity).id != hidden_child;
    };
    NetHarness net(9311, config);
    const NetId parent = net.spawn(at(0.0f));
    hidden_child = net.spawn(at(0.0f));
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(hidden_child), net.server_entity(parent)));
    net.connect();
    net.step(2);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(20);
    EXPECT_TRUE(net.client_entity(parent).has_value());
    EXPECT_FALSE(net.client_entity(hidden_child).has_value());
}

TEST(NetInterest, CaptureSerializesOncePerTickWhateverThePeers) {
    constexpr int kEntities = 5;
    int writes = 0;
    const auto write = [&writes](ReplicaWriter& writer, const Health& value) {
        ++writes;
        writer.bytes().write_i32(value.value);
    };
    const auto read = [](ReplicaReader& reader, Health& value) { return reader.bytes().read(value.value); };

    NetHarness net(9313);
    NetClientSide& second = net.add_client();
    NetClientSide& third = net.add_client();
    register_replicated<Health>(net.server, "Health", write, read);
    register_health(net.client);
    register_health(second.world);
    register_health(third.world);
    for (int i = 0; i < kEntities; ++i) {
        net.server.emplace<Health>(net.server_entity(net.spawn()), Health{i});
    }
    net.connect();
    net.step(3); // all connected and welcomed; one send tick so far
    ASSERT_EQ(net.server.ctx<NetServer>().peers().size(), 3u);

    writes = 0;
    net.step(20); // 10 send ticks
    EXPECT_EQ(writes, 10 * kEntities);
    EXPECT_TRUE(third.entity(net.client.get<NetIdentity>(net.spawned.at(0).entity).id).has_value());
}

TEST(NetInterest, ReenteringCarriesFreshState) {
    NetHarness net(9314, grid_config());
    register_health(net.server);
    register_health(net.client);
    const NetId mob = net.spawn(at(0.0f));
    net.server.emplace<Health>(net.server_entity(mob), Health{10});
    net.connect();
    net.step(2);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(20);
    ASSERT_EQ(net.client.get<Health>(*net.client_entity(mob)).value, 10);

    place(net, mob, 500.0f);
    net.step(20);
    ASSERT_FALSE(net.client_entity(mob).has_value());
    net.server.get<Health>(net.server_entity(mob)).value = 3;

    place(net, mob, 0.0f);
    net.step(20);
    ASSERT_TRUE(net.client_entity(mob).has_value());
    EXPECT_EQ(net.client.get<Health>(*net.client_entity(mob)).value, 3);
    EXPECT_FLOAT_EQ(net.client.get<Transform>(*net.client_entity(mob)).position.x, 0.0f);
    EXPECT_EQ(net.spawned.size(), 2u);
    EXPECT_EQ(net.despawned.size(), 1u);
}

TEST(NetInterest, LeavingAndReenteringWithSnapshotsInFlightEndsConsistent) {
    NetHarness net(9315, grid_config());
    net.server_link.set_latency_ms(50.0);
    net.client_link.set_latency_ms(50.0);
    const NetId mob = net.spawn(at(0.0f));
    net.connect();
    net.step(2);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(30);
    ASSERT_TRUE(net.client_entity(mob).has_value());

    // Faster than a round trip.
    for (int i = 0; i < 12; ++i) {
        place(net, mob, i % 2 == 0 ? 500.0f : 0.0f);
        net.step(2);
    }
    place(net, mob, 0.0f);
    net.step(40);
    ASSERT_TRUE(net.client_entity(mob).has_value());
    EXPECT_FLOAT_EQ(net.client.get<Transform>(*net.client_entity(mob)).position.x, 0.0f);
    // Spawns and despawns balance out: one entity is alive.
    EXPECT_EQ(net.spawned.size(), net.despawned.size() + 1u);

    place(net, mob, 500.0f);
    net.step(40);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    EXPECT_EQ(net.spawned.size(), net.despawned.size());
    EXPECT_EQ(testing::snapshots_sent(net.server_link).back().removed, 0u);
}

TEST(NetInterest, DestroyedOutOfViewListsNothing) {
    NetHarness net(9316, grid_config());
    const NetId far = net.spawn(at(9000.0f));
    net.connect();
    net.step(10);
    net.server.destroy(net.server_entity(far));
    net.step(10);
    for (const auto& snapshot : testing::snapshots_sent(net.server_link)) {
        EXPECT_EQ(snapshot.removed, 0u);
        EXPECT_EQ(snapshot.entities, 0u);
    }
}

TEST(NetInterest, DestroyedInViewDespawnsOnThePeerSeeingIt) {
    NetHarness net(9317, grid_config());
    const NetId mob = net.spawn(at(0.0f));
    net.connect();
    net.step(2);
    const glm::vec3 focus{0.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(peer_of(net, 0), std::span(&focus, 1));
    net.step(20);
    ASSERT_TRUE(net.client_entity(mob).has_value());
    net.server.destroy(net.server_entity(mob));
    net.step(20);
    EXPECT_FALSE(net.client_entity(mob).has_value());
    EXPECT_EQ(net.despawned.size(), 1u);
}

TEST(NetInterest, SnapshotsKeepComingWithNothingRelevant) {
    NetHarness net(9318, grid_config());
    net.spawn(at(0.0f));
    net.connect();
    net.step(20);
    // The client's clock and input report ride on them.
    EXPECT_GE(testing::snapshots_sent(net.server_link).size(), 9u);
    EXPECT_EQ(net.client.ctx<NetClient>().newest_tick(), net.tick());
}

} // namespace
} // namespace engine::net
