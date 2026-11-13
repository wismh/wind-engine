#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/core/time.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/net/fluid_sync_2d.h>
#include <engine/net/net_client.h>
#include <engine/net/net_server.h>

#include <array>
#include <map>
#include <vector>

// LQ-64: a fluid simulated by every peer from the server's tick stream.

namespace {

using engine::net::FluidInput2D;
using engine::net::testing::NetClientSide;
using engine::net::testing::NetHarness;

constexpr std::uint32_t kTerrainId = 1;
constexpr std::uint32_t kWaterId = 2;

// The level every peer loads the same: an SDF tank and an empty fluid, both with a sync id. With `sleep` the fluid lets
// resting particles sleep (LQ-38).
void build_level(engine::ecs::World& world, bool sleep = false) {
    engine::register_engine_systems(world);
    world.ctx<engine::Physics2DConfig>().gravity = {0.f, -10.f, 0.f};
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = 0.1f, .band = 0.4f}};
    const std::array<glm::vec2, 4> outer{glm::vec2{-1.f, -1.f}, glm::vec2{5.f, -1.f}, glm::vec2{5.f, 3.f},
                                         glm::vec2{-1.f, 3.f}};
    const std::array<glm::vec2, 4> inner{glm::vec2{0.f, 0.f}, glm::vec2{4.f, 0.f}, glm::vec2{4.f, 5.f},
                                         glm::vec2{0.f, 5.f}};
    (void)field.fill_polygon(outer);
    (void)field.carve_polygon(inner);
    const engine::ecs::Entity terrain = world.create();
    world.emplace<engine::Transform>(terrain, engine::Transform{});
    world.emplace<engine::SdfCollider2D>(terrain, engine::SdfCollider2D{.field = std::move(field)});
    world.emplace<engine::net::FluidSyncId2D>(terrain, engine::net::FluidSyncId2D{.id = kTerrainId});
    const engine::ecs::Entity water = world.create();
    world.emplace<engine::Fluid2D>(water, engine::Fluid2DParams{.radius = 0.05f, .sleep = sleep});
    world.emplace<engine::net::FluidSyncId2D>(water, engine::net::FluidSyncId2D{.id = kWaterId});
}

const engine::Fluid2D& water_of(engine::ecs::World& world) {
    const engine::Fluid2D* found = nullptr;
    world.view<engine::Fluid2D, engine::net::FluidSyncId2D>().each(
            [&](const engine::Fluid2D& fluid, const engine::net::FluidSyncId2D& id) {
                if (id.id == kWaterId) {
                    found = &fluid;
                }
            });
    return *found;
}

engine::Fluid2D& water_of_mut(engine::ecs::World& world) {
    return const_cast<engine::Fluid2D&>(water_of(world));
}

struct Checks {
    std::size_t compared = 0;
    std::size_t mismatched = 0;
};

// Compares a client's fluid with the server's at the tick the client is at, when the server recorded it.
void check(engine::ecs::World& client, const std::map<engine::net::Tick, std::uint64_t>& server_hashes, Checks& out) {
    const auto stats = engine::net::fluid_sync_client_stats_2d(client);
    if (stats.synced_tick == engine::net::kNoTick) {
        return;
    }
    const auto it = server_hashes.find(stats.synced_tick);
    if (it == server_hashes.end()) {
        return;
    }
    ++out.compared;
    if (water_of(client).state_hash() != it->second) {
        ++out.mismatched;
    }
}

}

TEST(NetFluidSync, ServerAndTwoClientsKeepTheSameFluidAndAForcedDesyncIsRepaired) {
    NetHarness net{46100};
    build_level(net.server);
    build_level(net.client);
    net.connect();
    engine::net::install_fluid_sync_server_2d(net.server, net.server_link,
                                              engine::net::FluidSyncConfig2D{.hash_interval = 10});
    engine::net::install_fluid_sync_client_2d(net.client, net.client_link,
                                              engine::net::FluidSyncConfig2D{.hash_interval = 10});

    // A dynamic box that falls into the water (the water pushes it back on the server only) and a kinematic paddle.
    const engine::ecs::Entity box = net.server.create();
    net.server.emplace<engine::Transform>(box, engine::Transform{.position = {2.5f, 2.f, 0.f}});
    net.server.emplace<engine::BoxCollider2D>(box, engine::BoxCollider2D{.size = {0.4f, 0.3f, 1.f}});
    net.server.emplace<engine::RigidBody2D>(box, engine::RigidBody2D{.type = engine::BodyType::Dynamic, .mass = 0.06f});
    (void)engine::net::replicate(net.server, box);
    const engine::ecs::Entity paddle = net.server.create();
    net.server.emplace<engine::Transform>(paddle, engine::Transform{.position = {3.5f, 0.4f, 0.f}});
    net.server.emplace<engine::CircleCollider2D>(paddle, engine::CircleCollider2D{.radius = 0.2f});
    net.server.emplace<engine::RigidBody2D>(
            paddle, engine::RigidBody2D{.velocity = {-0.5f, 0.f, 0.f}, .type = engine::BodyType::Kinematic});
    (void)engine::net::replicate(net.server, paddle);

    std::map<engine::net::Tick, std::uint64_t> server_hashes;
    Checks first;
    Checks late;
    NetClientSide* joiner = nullptr;
    for (int frame = 0; frame < 600; ++frame) {
        if (frame == 10) {
            engine::net::queue_fluid_input_2d(
                    net.server, FluidInput2D{.kind = FluidInput2D::Kind::SpawnBox, .target = kWaterId,
                                             .a = {0.f, 0.f}, .b = {1.2f, 1.5f}});
        }
        if (frame == 90 || frame == 200) {
            engine::net::queue_fluid_input_2d(
                    net.server, FluidInput2D{.kind = FluidInput2D::Kind::CarveCircle, .target = kTerrainId,
                                             .a = {frame == 90 ? 4.2f : -0.2f, 0.3f}, .value = 0.45f});
        }
        if (frame == 150) {
            joiner = &net.add_client();
            build_level(joiner->world);
            engine::net::install_fluid_sync_client_2d(joiner->world, joiner->link,
                                                      engine::net::FluidSyncConfig2D{.hash_interval = 10});
        }
        if (frame == 400) {
            // Force a desync on the first client: one particle nudged. Detected at the next hashed tick.
            engine::Fluid2D& water = water_of_mut(net.client);
            std::vector<std::uint8_t> state = water.save_state();
            state[16] ^= 1u;  // the lowest bit of the first particle's x
            ASSERT_TRUE(water.load_state(state));
        }
        net.step();
        server_hashes[net.tick()] = water_of(net.server).state_hash();
        if (frame < 400 || frame > 470) {
            check(net.client, server_hashes, first);
        }
        if (joiner != nullptr) {
            check(joiner->world, server_hashes, late);
        }
    }

    EXPECT_GT(water_of(net.server).count(), 100u);
    EXPECT_GT(first.compared, 400u);
    EXPECT_EQ(first.mismatched, 0u) << "the first client's fluid is the server's at every tick it reached";
    EXPECT_GT(late.compared, 300u);
    EXPECT_EQ(late.mismatched, 0u) << "the late joiner too, from the state it was sent";
    const auto stats = engine::net::fluid_sync_client_stats_2d(net.client);
    EXPECT_EQ(stats.desyncs, 1u) << "the nudge was caught once";
    EXPECT_EQ(stats.states_loaded, 2u) << "the first state and the repair";
    EXPECT_EQ(engine::net::fluid_sync_client_stats_2d(joiner->world).desyncs, 0u);
    // The server moved the box with the water's reaction; the clients pushed nothing.
    EXPECT_GT(net.server.get<engine::Transform>(box).position.y, 0.0f);
}

TEST(NetFluidSync, ASleepingFluidStaysTheSameOnEveryPeer) {
    // Sleep state is simulation state: the clients' sleepers, rest counters and boundary marks follow the server's
    // through falling asleep, a late join that loads them from the state, a carve that wakes part of the pool and a
    // kinematic paddle that keeps the water near it awake.
    NetHarness net{46150};
    build_level(net.server, true);
    build_level(net.client, true);
    net.connect();
    engine::net::install_fluid_sync_server_2d(net.server, net.server_link,
                                              engine::net::FluidSyncConfig2D{.hash_interval = 10});
    engine::net::install_fluid_sync_client_2d(net.client, net.client_link,
                                              engine::net::FluidSyncConfig2D{.hash_interval = 10});
    const engine::ecs::Entity paddle = net.server.create();
    net.server.emplace<engine::Transform>(paddle, engine::Transform{.position = {3.5f, 0.3f, 0.f}});
    net.server.emplace<engine::CircleCollider2D>(paddle, engine::CircleCollider2D{.radius = 0.15f});
    net.server.emplace<engine::RigidBody2D>(paddle, engine::RigidBody2D{.type = engine::BodyType::Kinematic});
    (void)engine::net::replicate(net.server, paddle);

    std::map<engine::net::Tick, std::uint64_t> server_hashes;
    Checks first;
    Checks late;
    NetClientSide* joiner = nullptr;
    std::size_t asleep_at_join = 0;
    std::size_t asleep_after_carve = 0;
    for (int frame = 0; frame < 1500; ++frame) {
        if (frame == 10) {
            engine::net::queue_fluid_input_2d(
                    net.server, FluidInput2D{.kind = FluidInput2D::Kind::SpawnBox, .target = kWaterId,
                                             .a = {0.f, 0.f}, .b = {1.2f, 1.5f}});
        }
        if (frame == 1100) {
            joiner = &net.add_client();
            build_level(joiner->world, true);
            engine::net::install_fluid_sync_client_2d(joiner->world, joiner->link,
                                                      engine::net::FluidSyncConfig2D{.hash_interval = 10});
            asleep_at_join = water_of(net.server).sleeping_count();
        }
        if (frame == 1300) {
            engine::net::queue_fluid_input_2d(
                    net.server, FluidInput2D{.kind = FluidInput2D::Kind::CarveCircle, .target = kTerrainId,
                                             .a = {1.f, 0.f}, .value = 0.3f});
        }
        net.step();
        if (frame == 1302) {
            asleep_after_carve = water_of(net.server).sleeping_count();
        }
        server_hashes[net.tick()] = water_of(net.server).state_hash();
        check(net.client, server_hashes, first);
        if (joiner != nullptr) {
            check(joiner->world, server_hashes, late);
        }
    }

    const std::size_t count = water_of(net.server).count();
    EXPECT_GT(asleep_at_join, count / 2) << "the pool was mostly asleep when the late client joined";
    EXPECT_LT(asleep_at_join, count) << "the water near the paddle stays awake";
    EXPECT_LT(asleep_after_carve, asleep_at_join) << "the carve woke part of it";
    EXPECT_GT(first.compared, 1000u);
    EXPECT_EQ(first.mismatched, 0u);
    EXPECT_GT(late.compared, 300u);
    EXPECT_EQ(late.mismatched, 0u);
    EXPECT_EQ(engine::net::fluid_sync_client_stats_2d(net.client).desyncs, 0u);
    EXPECT_EQ(engine::net::fluid_sync_client_stats_2d(joiner->world).desyncs, 0u);
}

TEST(NetFluidSync, MovableBoundariesFromTheStreamStepTheSameAsTheWorld) {
    // The client steps with the server's movable colliders instead of its own: same bits.
    auto build = [](engine::ecs::World& world) {
        build_level(world);
        engine::install_fluid_2d(world);
        world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
        (void)water_of_mut(world).spawn_box({0.f, 0.f}, {1.5f, 1.5f});
        const engine::ecs::Entity body = world.create();
        world.emplace<engine::Transform>(body, engine::Transform{.position = {1.f, 1.8f, 0.f},
                                                                 .rotation = engine::rotation_z(0.4f)});
        world.emplace<engine::BoxCollider2D>(body, engine::BoxCollider2D{.size = {0.5f, 0.2f, 1.f}});
        world.emplace<engine::RigidBody2D>(body, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
        return body;
    };
    engine::ecs::World a;
    const engine::ecs::Entity body = build(a);
    engine::ecs::World b;
    build_level(b);
    b.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    (void)water_of_mut(b).spawn_box({0.f, 0.f}, {1.5f, 1.5f});
    for (int i = 0; i < 90; ++i) {
        // a: the authoritative step through its own schedule; b: the same physics poses, handed in.
        engine::run_physics_2d(a);
        const std::vector<engine::FluidBoundary2D> movable = engine::movable_fluid_boundaries_2d(a);
        engine::run_fluid_2d(a);
        engine::run_fluid_2d(b, nullptr,
                             engine::FluidStepOptions2D{.push_bodies = false, .movable_from_world = false,
                                                        .movable = movable});
        ASSERT_EQ(water_of(a).state_hash(), water_of(b).state_hash()) << "tick " << i;
    }
    (void)body;
}
