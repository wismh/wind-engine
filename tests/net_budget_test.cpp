#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_client.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>
#include <engine/net/prediction.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <span>
#include <vector>

namespace engine::net {
namespace {

using testing::NetHarness;
using testing::snapshots_sent;

constexpr int kCrowd = 200; // far more moving Transforms than fit one packet

std::vector<NetId> spawn_crowd(NetHarness& net, int count = kCrowd) {
    std::vector<NetId> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        ids.push_back(net.spawn());
    }
    return ids;
}

// Every replicated entity's x follows the tick while `running`.
void move_all_with_tick(NetHarness& net, const bool* running = nullptr) {
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [running](ecs::World& world) {
        if (running != nullptr && !*running) {
            return;
        }
        const float x = static_cast<float>(world.ctx<NetServer>().tick());
        for (const ecs::Entity entity : world.view<NetIdentity, Transform>()) {
            world.get<Transform>(entity).position.x = x;
        }
    });
}

void set_latency(NetHarness& net, double ms) {
    net.server_link.set_latency_ms(ms);
    net.client_link.set_latency_ms(ms);
}

TEST(NetBudget, SnapshotNeverExceedsTheBudget) {
    NetHarness net(9401);
    spawn_crowd(net);
    move_all_with_tick(net);
    net.connect();
    net.step(120);
    const auto snapshots = snapshots_sent(net.server_link);
    ASSERT_GE(snapshots.size(), 50u);
    bool deferred = false;
    for (const auto& snapshot : snapshots) {
        EXPECT_LE(snapshot.size, kSnapshotMtuBudget) << "tick " << snapshot.tick;
        EXPECT_GT(snapshot.entities, 0u);
        deferred = deferred || snapshot.entities < kCrowd;
    }
    EXPECT_TRUE(deferred);
    EXPECT_GT(net.server.ctx<NetServer>().peer_stats(net.server.ctx<NetServer>().peers().at(0))->deferred, 0u);
}

TEST(NetBudget, ConfiguredBudgetApplies) {
    NetServerConfig config;
    config.snapshot_budget_bytes = 400;
    NetHarness net(9402, config);
    spawn_crowd(net, 50);
    move_all_with_tick(net);
    net.connect();
    net.step(60);
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        EXPECT_LE(snapshot.size, 400u);
    }
}

TEST(NetBudget, LowPeerRateShrinksTheSnapshot) {
    NetServerConfig config;
    config.peer_bytes_per_second = 6000; // 200 bytes per snapshot at 30 a second, less the payloads' tenth
    NetHarness net(9403, config);
    spawn_crowd(net, 50);
    move_all_with_tick(net);
    net.connect();
    net.step(60);
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        EXPECT_LE(snapshot.size, 180u) << "tick " << snapshot.tick;
    }
}

TEST(NetBudget, OversizedRelevantSetConverges) {
    NetHarness net(9404);
    const std::vector<NetId> ids = spawn_crowd(net);
    bool running = true;
    move_all_with_tick(net, &running);
    net.connect();
    net.step(60);

    running = false;
    const float final_x = net.server.get<Transform>(net.server_entity(ids[0])).position.x;
    net.step(120);
    for (const NetId id : ids) {
        const auto entity = net.client_entity(id);
        ASSERT_TRUE(entity.has_value()) << id;
        EXPECT_FLOAT_EQ(net.client.get<Transform>(*entity).position.x, final_x) << id;
    }
    // Everything acknowledged: nothing left to send.
    EXPECT_EQ(snapshots_sent(net.server_link).back().entities, 0u);
}

TEST(NetBudget, EveryoneArrivesFromAJoin) {
    NetHarness net(9405);
    const std::vector<NetId> ids = spawn_crowd(net);
    net.connect();
    // 65 bytes a new entity, about 18 a snapshot: 200 take 12 snapshots, with nothing lost and no rewrites needed.
    net.step(40);
    for (const NetId id : ids) {
        EXPECT_TRUE(net.client_entity(id).has_value()) << id;
    }
    EXPECT_EQ(net.spawned.size(), ids.size());
    EXPECT_TRUE(net.despawned.empty());
}

TEST(NetBudget, NoEntityStarves) {
    NetServerConfig config;
    config.interest.priority = [](const ecs::World& world, ecs::Entity entity, PeerId) {
        // 100 to 1 between the first entity and all the others.
        return world.get<NetIdentity>(entity).id == 1 ? 1.0f : 0.01f;
    };
    NetHarness net(9406, config);
    const std::vector<NetId> ids = spawn_crowd(net);
    move_all_with_tick(net);
    net.connect();
    net.step(600);

    std::map<NetId, Tick> last;
    std::map<NetId, Tick> longest;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        for (const NetId id : snapshot.ids) {
            longest[id] = std::max(longest[id], snapshot.tick - last[id]);
            last[id] = snapshot.tick;
        }
    }
    for (const NetId id : ids) {
        ASSERT_TRUE(last.contains(id)) << id;
        // About 23 updates a snapshot: every 200-entity round takes under 10 snapshots, 20 ticks.
        EXPECT_LE(longest[id], 40u) << id;
    }
    EXPECT_LE(longest[1], longest[2]);
}

// The regression test of the per-entity design: with snapshots in flight, a snapshot that leaves an entity out must not
// take it back to an older value, or back out of existence.
TEST(NetBudget, ValuesNeverGoBackward) {
    NetHarness net(9407);
    set_latency(net, 50.0);
    const std::vector<NetId> ids = spawn_crowd(net);
    move_all_with_tick(net);
    net.connect();

    std::map<NetId, float> seen;
    int moves = 0;
    for (int frame = 0; frame < 300; ++frame) {
        net.step();
        for (const NetId id : ids) {
            const auto entity = net.client_entity(id);
            if (!entity.has_value()) {
                continue;
            }
            const float x = net.client.get<Transform>(*entity).position.x;
            const auto it = seen.find(id);
            if (it != seen.end()) {
                ASSERT_GE(x, it->second - 1e-3f) << "entity " << id << " at frame " << frame;
                moves += x > it->second ? 1 : 0;
            }
            seen[id] = x;
        }
    }
    EXPECT_EQ(seen.size(), ids.size());
    EXPECT_GT(moves, kCrowd);
    // Nothing left and came back.
    EXPECT_EQ(net.spawned.size(), ids.size());
    EXPECT_TRUE(net.despawned.empty());
}

TEST(NetBudget, ValuesNeverGoBackwardUnderLossAndReorder) {
    NetHarness net(9408);
    set_latency(net, 50.0);
    net.server_link.set_jitter_ms(30, 7);
    std::minstd_rand random(11);
    net.server_link.set_drop(
            [&random](std::size_t, std::span<const std::uint8_t>) { return random() % 5 == 0; });
    const std::vector<NetId> ids = spawn_crowd(net, 60);
    bool running = true;
    move_all_with_tick(net, &running);
    net.connect();

    std::map<NetId, float> seen;
    for (int frame = 0; frame < 400; ++frame) {
        net.step();
        for (const NetId id : ids) {
            if (const auto entity = net.client_entity(id)) {
                const float x = net.client.get<Transform>(*entity).position.x;
                const auto it = seen.find(id);
                if (it != seen.end()) {
                    // Interpolation between two held states, re-derived when a late snapshot lands, can only hold or
                    // step forward. A late snapshot may make an entity's next value arrive sooner, never older.
                    ASSERT_GE(x, it->second - 1e-3f) << "entity " << id << " at frame " << frame;
                }
                seen[id] = x;
            }
        }
    }
    EXPECT_TRUE(net.despawned.empty());

    running = false;
    net.server_link.set_drop({});
    net.server_link.set_jitter_ms(0);
    const float final_x = net.server.get<Transform>(net.server_entity(ids[0])).position.x;
    net.step(200);
    for (const NetId id : ids) {
        ASSERT_TRUE(net.client_entity(id).has_value()) << id;
        EXPECT_FLOAT_EQ(net.client.get<Transform>(*net.client_entity(id)).position.x, final_x) << id;
    }
}

TEST(NetBudget, CloserEntitiesUpdateMoreOften) {
    NetServerConfig config;
    config.interest.cell_size = 10.0f;
    config.interest.radius_cells = 20;
    NetHarness net(9409, config);
    std::vector<NetId> near;
    std::vector<NetId> far;
    for (int i = 0; i < 60; ++i) {
        near.push_back(net.spawn(Transform{.position = {5.0f, 0.0f, 0.0f}}));
        far.push_back(net.spawn(Transform{.position = {5.0f, 150.0f, 0.0f}}));
    }
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [](ecs::World& world) {
        const float z = static_cast<float>(world.ctx<NetServer>().tick()) * 0.001f;
        for (const ecs::Entity entity : world.view<NetIdentity, Transform>()) {
            world.get<Transform>(entity).position.z = z;
        }
    });
    net.connect();
    net.step(2);
    const glm::vec3 focus{5.0f, 0.0f, 0.0f};
    net.server.ctx<NetServer>().set_peer_focus(net.server.ctx<NetServer>().peers().at(0), std::span(&focus, 1));
    net.step(300);

    std::size_t near_writes = 0;
    std::size_t far_writes = 0;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        if (snapshot.tick < 60) {
            continue;
        }
        for (const NetId id : snapshot.ids) {
            (std::find(near.begin(), near.end(), id) != near.end() ? near_writes : far_writes) += 1;
        }
    }
    EXPECT_GT(far_writes, 0u);
    EXPECT_GT(near_writes, far_writes * 4);
}

TEST(NetBudget, OverBudgetRecordStillGoes) {
    struct Big {
        std::vector<std::uint8_t> bytes{};
    };
    NetHarness net(9410);
    const auto write = [](ReplicaWriter& writer, const Big& big) {
        writer.bytes().write_u16(static_cast<std::uint16_t>(big.bytes.size()));
        writer.bytes().append(big.bytes);
    };
    const auto read = [](ReplicaReader& reader, Big& big) {
        const auto size = reader.bytes().read_u16();
        if (!size) {
            return false;
        }
        const auto bytes = reader.bytes().read_bytes(*size);
        if (!bytes) {
            return false;
        }
        big.bytes.assign(bytes->begin(), bytes->end());
        return true;
    };
    register_replicated<Big>(net.server, "Big", write, read);
    register_replicated<Big>(net.client, "Big", write, read);
    const NetId id = net.spawn();
    net.server.emplace<Big>(net.server_entity(id), Big{std::vector<std::uint8_t>(3000, 9)});
    net.connect();
    net.step(20);
    ASSERT_TRUE(net.client_entity(id).has_value());
    EXPECT_EQ(net.client.get<Big>(*net.client_entity(id)).bytes.size(), 3000u);
    EXPECT_GT(snapshots_sent(net.server_link).front().size, kSnapshotMtuBudget);
}

struct Step {
    std::int32_t dx{0};
};

struct Walker {
    float x{0.0f};
    bool operator==(const Walker&) const = default;
};

PredictionDesc<Step, Walker> walker_desc() {
    return PredictionDesc<Step, Walker>{
            .name = "Step",
            .write_input = [](ByteWriter& out, const Step& step) { out.write_i32(step.dx); },
            .read_input = [](ByteReader& in, Step& step) { return in.read(step.dx); },
            .step = [](ecs::World& world, ecs::Entity entity,
                            const Step& step) { world.get<Walker>(entity).x += static_cast<float>(step.dx); },
            .collect = [](ecs::World&, ecs::Entity) { return Step{1}; },
            .capture = [](const ecs::World& world, ecs::Entity entity) { return world.get<Walker>(entity); },
            .restore = [](ecs::World& world, ecs::Entity entity,
                               const Walker& state) { world.get<Walker>(entity) = state; },
    };
}

void register_walker(ecs::World& world) {
    register_replicated<Walker>(
            world, "Walker", [](ReplicaWriter& out, const Walker& walker) { out.bytes().write_float(walker.x); },
            [](ReplicaReader& in, Walker& walker) { return in.bytes().read(walker.x); });
}

TEST(NetBudget, PredictedEntityIsNeverDeferred) {
    NetHarness net(9411);
    set_latency(net, 50.0);
    register_walker(net.server);
    register_walker(net.client);
    register_prediction(net.server, walker_desc());
    register_prediction(net.client, walker_desc());
    spawn_crowd(net);
    move_all_with_tick(net);
    net.connect();
    net.step(10);

    const PeerId peer = net.server.ctx<NetServer>().peers().at(0);
    const ecs::Entity entity = net.server.create();
    net.server.emplace<Transform>(entity);
    net.server.emplace<Walker>(entity);
    const NetId mine = replicate(net.server, entity, peer);
    const Tick owned_from = net.tick();
    net.step(240);

    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        if (snapshot.tick > owned_from) {
            EXPECT_NE(std::find(snapshot.ids.begin(), snapshot.ids.end(), mine), snapshot.ids.end())
                    << "tick " << snapshot.tick;
            EXPECT_LE(snapshot.size, kSnapshotMtuBudget);
        }
    }
    EXPECT_TRUE(net.client.ctx<NetClient>().is_predicted(mine));
    const auto& stats = net.client.ctx<NetClient>().prediction_stats();
    EXPECT_GT(stats.matched, 0u);
    EXPECT_EQ(stats.snaps, 0u);
}

TEST(NetBudget, OwnedEntityWithoutPredictionStillRanksFirstButCanWait) {
    NetHarness net(9412);
    spawn_crowd(net);
    move_all_with_tick(net);
    net.connect();
    net.step(10);
    const PeerId peer = net.server.ctx<NetServer>().peers().at(0);
    const ecs::Entity entity = net.server.create();
    net.server.emplace<Transform>(entity);
    const NetId mine = replicate(net.server, entity, peer);
    const std::size_t before = snapshots_sent(net.server_link).size();
    net.step(120);
    const auto snapshots = snapshots_sent(net.server_link);
    std::size_t written = 0;
    for (std::size_t i = before; i < snapshots.size(); ++i) {
        written += std::count(snapshots[i].ids.begin(), snapshots[i].ids.end(), mine);
    }
    EXPECT_GT(written, 0u);
    EXPECT_TRUE(net.client_entity(mine).has_value());
}

TEST(NetBudget, ChildParentResolvesUnderPressure) {
    NetHarness net(9413);
    spawn_crowd(net);
    const NetId parent = net.spawn();
    const NetId child = net.spawn();
    ASSERT_TRUE(ecs::set_parent(net.server, net.server_entity(child), net.server_entity(parent)));
    net.connect();
    net.step(60);
    ASSERT_TRUE(net.client_entity(child).has_value());
    ASSERT_TRUE(net.client_entity(parent).has_value());
    EXPECT_EQ(net.client.get<Parent>(*net.client_entity(child)).entity, *net.client_entity(parent));

    bool child_before_parent = false;
    bool parent_seen = false;
    for (const auto& snapshot : snapshots_sent(net.server_link)) {
        const bool has_parent = std::find(snapshot.ids.begin(), snapshot.ids.end(), parent) != snapshot.ids.end();
        const bool has_child = std::find(snapshot.ids.begin(), snapshot.ids.end(), child) != snapshot.ids.end();
        child_before_parent = child_before_parent || (has_child && !has_parent && !parent_seen);
        parent_seen = parent_seen || has_parent;
    }
    EXPECT_FALSE(child_before_parent);
}

} // namespace
} // namespace engine::net
