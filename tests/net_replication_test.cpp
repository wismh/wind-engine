#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_client.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_server.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace engine::net {
namespace {

using testing::NetHarness;
using testing::snapshots_sent;

struct Health {
    int value{0};
};

struct Tint {
    float value{0.0f};
};

void register_health(ecs::World& world) {
    register_replicated<Health>(
            world, "Health", [](ReplicaWriter& writer, const Health& value) { writer.bytes().write_i32(value.value); },
            [](ReplicaReader& reader, Health& value) { return reader.bytes().read(value.value); });
}

void register_tint(ecs::World& world) {
    register_replicated<Tint>(
            world, "Tint", [](ReplicaWriter& writer, const Tint& value) { writer.bytes().write_float(value.value); },
            [](ReplicaReader& reader, Tint& value) { return reader.bytes().read(value.value); },
            ReplicatedOptions<Tint>{
                    .interpolate = [](const Tint& from, const Tint& to,
                                           float t) { return Tint{from.value + (to.value - from.value) * t}; },
            });
}

// Moves `id` so that its x equals the server tick, every fixed step.
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

TEST(NetReplication, TickAdvancesOncePerFixedStep) {
    NetHarness net(9101);
    net.connect();
    for (int i = 0; i < 5; ++i) {
        net.server.run(ecs::Schedule::Fixed);
    }
    EXPECT_EQ(net.tick(), 5u);
    net.server.run(ecs::Schedule::Frame);
    EXPECT_EQ(net.tick(), 5u);
}

TEST(NetReplication, WelcomeThenSnapshotsEveryInterval) {
    NetHarness net(9102);
    net.connect();
    net.step(7);

    const auto& client = net.client.ctx<NetClient>();
    ASSERT_TRUE(client.welcomed());
    const auto peers = net.server.ctx<NetServer>().peers();
    ASSERT_EQ(peers.size(), 1u);
    EXPECT_EQ(client.local_peer(), peers[0]);

    std::vector<Tick> ticks;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        ticks.push_back(snapshot.tick);
    }
    EXPECT_EQ(ticks, (std::vector<Tick>{2, 4, 6}));
    EXPECT_EQ(client.newest_tick(), 6u);
}

TEST(NetReplication, InterpolatesAtRenderTime) {
    NetHarness net(9103);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    net.connect();
    net.step(20);

    // The client shows the server 0.1 s (6 ticks) behind its newest snapshot estimate; x follows the tick
    // exactly, between snapshots too.
    for (int i = 0; i < 12; ++i) {
        net.step();
        const double expected = static_cast<double>(net.tick()) - 5.0;
        EXPECT_NEAR(net.client.ctx<NetClient>().render_tick(), expected, 1e-4); // Time::delta_time is a float
        EXPECT_NEAR(client_x(net, id), expected, 1e-3);
    }
}

TEST(NetReplication, OutOfOrderSnapshotInsertedByTick) {
    NetHarness net(9104);
    net.spawn();
    net.connect();
    net.step(9);
    ASSERT_EQ(net.client.ctx<NetClient>().newest_tick(), 8u);

    net.server_link.hold();
    net.step(4); // snapshots 10 and 12 are held
    EXPECT_EQ(net.client.ctx<NetClient>().newest_tick(), 8u);
    net.server_link.release(/*reversed=*/true); // 12 arrives before 10
    net.step(1);

    const auto ticks = net.client.ctx<NetClient>().buffered_ticks();
    EXPECT_TRUE(std::is_sorted(ticks.begin(), ticks.end()));
    for (const Tick tick : {Tick{8}, Tick{10}, Tick{12}, Tick{14}}) {
        EXPECT_NE(std::find(ticks.begin(), ticks.end(), tick), ticks.end()) << tick;
    }
    EXPECT_EQ(net.client.ctx<NetClient>().newest_tick(), 14u);
}

TEST(NetReplication, DroppedSnapshotsInterpolateAcrossTheGap) {
    NetHarness net(9105);
    const NetId id = net.spawn();
    move_with_tick(net, id);
    net.server_link.set_drop([](std::size_t, std::span<const std::uint8_t> bytes) {
        const auto snapshot = testing::parse_snapshot(bytes);
        return snapshot && (snapshot->tick == 20 || snapshot->tick == 22);
    });
    net.connect();
    net.step(12);
    for (int i = 0; i < 24; ++i) {
        net.step();
        EXPECT_NEAR(client_x(net, id), static_cast<double>(net.tick()) - 5.0, 1e-3) << "tick " << net.tick();
    }
}

TEST(NetReplication, SpawnAndDespawnFollowRenderTime) {
    NetHarness net(9106);
    NetId id = kInvalidNetId;
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [&id](ecs::World& world) {
        const Tick tick = world.ctx<NetServer>().tick();
        if (tick == 10) {
            const ecs::Entity entity = world.create();
            world.emplace<Transform>(entity, Transform{});
            id = replicate(world, entity);
        } else if (tick == 20) {
            world.destroy(*find_entity(world, id));
        }
    });
    net.connect();
    for (int i = 0; i < 30; ++i) {
        net.step();
        const Tick tick = net.tick();
        // Render tick is tick - 5; the snapshot shown is the newest even tick at or before it.
        const bool shown = tick >= 15 && tick <= 24;
        EXPECT_EQ(id != kInvalidNetId && net.client_entity(id).has_value(), shown) << "tick " << tick;
    }
    ASSERT_EQ(net.spawned.size(), 1u);
    EXPECT_EQ(net.spawned[0].net_id, id);
    ASSERT_EQ(net.despawned.size(), 1u);
    EXPECT_EQ(net.despawned[0].net_id, id);
    EXPECT_EQ(net.despawned[0].entity, net.spawned[0].entity);
    EXPECT_FALSE(net.client.valid(net.spawned[0].entity));
}

TEST(NetReplication, ParentResolvesThroughNetEntityMap) {
    NetHarness net(9107);
    const NetId parent = net.spawn(Transform{.position = {1.0f, 0.0f, 0.0f}});
    const NetId child = net.spawn(Transform{.position = {0.0f, 2.0f, 0.0f}});
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(child), net.server_entity(parent)));
    net.connect();
    net.step(20);

    const auto client_parent = net.client_entity(parent);
    const auto client_child = net.client_entity(child);
    ASSERT_TRUE(client_parent && client_child);
    const auto* link = net.client.try_get<Parent>(*client_child);
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->entity, *client_parent);
    const auto* children = net.client.try_get<Children>(*client_parent);
    ASSERT_NE(children, nullptr);
    EXPECT_EQ(children->entities, std::vector<ecs::Entity>{*client_child});

    ecs::clear_parent(net.server, net.server_entity(child));
    net.step(10);
    EXPECT_EQ(net.client.try_get<Parent>(*client_child), nullptr);
    EXPECT_EQ(net.client.try_get<Children>(*client_parent), nullptr);
}

TEST(NetReplication, ReparentThenDestroyOldParentKeepsChild) {
    NetHarness net(9108);
    const NetId old_parent = net.spawn();
    const NetId new_parent = net.spawn();
    const NetId child = net.spawn();
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(child), net.server_entity(old_parent)));
    net.connect();
    net.step(20);
    const auto client_child = net.client_entity(child);
    ASSERT_TRUE(client_child.has_value());
    const std::size_t spawns_before = net.spawned.size();

    // One server tick reparents the child and destroys its old parent.
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(child), net.server_entity(new_parent)));
    net.server.destroy(net.server_entity(old_parent));
    net.step(20);

    EXPECT_EQ(net.client_entity(child), client_child); // the same entity, not re-made
    EXPECT_EQ(net.spawned.size(), spawns_before);
    EXPECT_FALSE(net.client_entity(old_parent).has_value());
    const auto* link = net.client.try_get<Parent>(*client_child);
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->entity, *net.client_entity(new_parent));
}

TEST(NetReplication, CustomReplicatedComponents) {
    NetHarness net(9109);
    for (ecs::World* world : {&net.server, &net.client}) {
        register_health(*world);
        register_tint(*world);
    }
    const NetId id = net.spawn();
    net.server.emplace<Health>(net.server_entity(id), Health{5});
    net.server.emplace<Tint>(net.server_entity(id), Tint{0.0f});
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [id](ecs::World& world) {
        const Tick tick = world.ctx<NetServer>().tick();
        const ecs::Entity entity = *find_entity(world, id);
        world.get<Tint>(entity).value = static_cast<float>(tick);
        if (tick == 30) {
            world.get<Health>(entity).value = 9;
        }
    });
    net.connect();
    net.step(20);

    const ecs::Entity entity = *net.client_entity(id);
    EXPECT_EQ(net.client.get<Health>(entity).value, 5);
    for (int i = 0; i < 20; ++i) {
        net.step();
        const Tick tick = net.tick();
        // Tint interpolates; Health steps when the timeline reaches tick 30.
        EXPECT_NEAR(net.client.get<Tint>(entity).value, static_cast<double>(tick) - 5.0, 1e-3);
        EXPECT_EQ(net.client.get<Health>(entity).value, tick >= 35 ? 9 : 5) << "tick " << tick;
    }
}

TEST(NetReplication, SchemaMismatchDisconnects) {
    NetHarness net(9110);
    register_health(net.client);
    net.spawn();
    net.connect();
    net.step(10);

    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed());
    EXPECT_TRUE(net.server.ctx<NetServer>().peers().empty());
    EXPECT_TRUE(net.client.ctx<NetEntityMap>().empty());
}

TEST(NetReplication, ClientDisconnectDespawnsReplicated) {
    NetHarness net(9111);
    const NetId a = net.spawn();
    const NetId b = net.spawn();
    const NetId c = net.spawn();
    net.connect();
    net.step(20);
    ASSERT_TRUE(net.client_entity(a) && net.client_entity(b) && net.client_entity(c));

    net.client_link.disconnect(kServerPeerId);
    net.step(2);

    EXPECT_EQ(net.despawned.size(), 3u);
    EXPECT_TRUE(net.client.ctx<NetEntityMap>().empty());
    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed());
    EXPECT_TRUE(net.server.ctx<NetServer>().peers().empty());
    for (const auto& event : net.despawned) {
        EXPECT_FALSE(net.client.valid(event.entity));
    }
}

TEST(NetReplication, OwnerReplicates) {
    NetHarness net(9112);
    net.connect();
    net.step(2);
    const PeerId peer = net.server.ctx<NetServer>().peers().at(0);
    const ecs::Entity entity = net.server.create();
    net.server.emplace<Transform>(entity, Transform{});
    const NetId id = replicate(net.server, entity, peer);
    net.step(20);

    const auto client_entity = net.client_entity(id);
    ASSERT_TRUE(client_entity.has_value());
    EXPECT_EQ(net.client.get<NetIdentity>(*client_entity).owner, net.client.ctx<NetClient>().local_peer());
}

TEST(NetReplication, ServerPrunesDestroyedEntities) {
    NetHarness net(9113);
    const NetId kept = net.spawn();
    const NetId gone = net.spawn();
    net.connect();
    net.server.destroy(net.server_entity(gone));
    net.step();
    EXPECT_EQ(net.server.ctx<NetEntityMap>().size(), 1u);
    EXPECT_TRUE(net.server.ctx<NetEntityMap>().contains(kept));
}

TEST(NetReplication, SoakWithLossReorderAndLatencyConverges) {
    NetHarness net(9114);
    std::minstd_rand loss(7);
    bool lossy = true;
    const auto drop = [&](std::size_t, std::span<const std::uint8_t>) { return lossy && loss() % 5 == 0; };
    for (testing::SimulatedLink* link : {&net.server_link, &net.client_link}) {
        link->set_latency_ms(50.0);
        link->set_jitter_ms(30, 3);
        link->set_drop(drop);
    }

    std::vector<NetId> ids;
    for (int i = 0; i < 12; ++i) {
        ids.push_back(net.spawn(Transform{.position = {static_cast<float>(i), 0.0f, 0.0f}}));
    }
    bool moving = true;
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [&](ecs::World& world) {
        if (!moving) {
            return;
        }
        const Tick tick = world.ctx<NetServer>().tick();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (const auto entity = find_entity(world, ids[i])) {
                auto& transform = world.get<Transform>(*entity);
                transform.position.y = std::sin(static_cast<float>(tick) * 0.05f + static_cast<float>(i));
                transform.rotation = glm::angleAxis(static_cast<float>(tick) * 0.01f, glm::vec3{0.0f, 0.0f, 1.0f});
            }
        }
        if (tick % 40 == 0) {
            world.destroy(*find_entity(world, ids.front()));
            ids.erase(ids.begin());
            const ecs::Entity entity = world.create();
            world.emplace<Transform>(entity, Transform{});
            ids.push_back(replicate(world, entity));
        }
    });
    net.connect();
    net.step(300);

    moving = false;
    lossy = false;
    net.step(60);

    std::size_t server_count = 0;
    for (const ecs::Entity entity : net.server.view<NetIdentity>()) {
        ++server_count;
        const NetId id = net.server.get<NetIdentity>(entity).id;
        const auto client_entity = net.client_entity(id);
        ASSERT_TRUE(client_entity.has_value()) << id;
        const auto& expected = net.server.get<Transform>(entity);
        const auto& actual = net.client.get<Transform>(*client_entity);
        EXPECT_NEAR(actual.position.x, expected.position.x, 1e-4f);
        EXPECT_NEAR(actual.position.y, expected.position.y, 1e-4f);
        EXPECT_NEAR(std::abs(glm::dot(actual.rotation, expected.rotation)), 1.0f, 1e-4f);
    }
    EXPECT_EQ(net.client.ctx<NetEntityMap>().size(), server_count);
    EXPECT_GT(net.spawned.size(), server_count); // entities came and went during the soak
}

} // namespace
} // namespace engine::net
