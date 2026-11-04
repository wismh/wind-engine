#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/core/input_system.h>
#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_client.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>
#include <engine/net/prediction.h>
#include <engine/net/replication_registry.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace engine::net {
namespace {

using testing::NetHarness;

struct Move {
    std::int32_t dx{0};
};

// The predicted state, replicated as a stepped component.
struct Body {
    float x{0.0f};
    bool operator==(const Body&) const = default;
};

// What the hooks of one world saw. The client's script picks the input per predicted tick.
struct Log {
    // Server: each step, the tick and the input it ran with.
    std::vector<std::pair<Tick, std::int32_t>> server_steps{};
    // Client: the input collected for each predicted tick.
    std::map<Tick, std::int32_t> collected{};
    std::function<std::int32_t(Tick)> script{};
    int client_steps{0};
    int replayed_steps{0};
    int corrections{0};
    float last_correction{0.0f};
    InputSystem* input{nullptr};
    ActionId move_x{};
};

PredictionDesc<Move, Body> move_desc() {
    return PredictionDesc<Move, Body>{
            .name = "Move",
            .write_input = [](ByteWriter& out, const Move& move) { out.write_i32(move.dx); },
            .read_input = [](ByteReader& in, Move& move) { return in.read(move.dx); },
            .step =
                    [](ecs::World& world, ecs::Entity entity, const Move& move) {
                        Log& log = world.ctx<Log>();
                        if (const NetServer* server = world.try_ctx<NetServer>()) {
                            log.server_steps.emplace_back(server->tick(), move.dx);
                        } else {
                            ++log.client_steps;
                            if (world.ctx<NetClient>().replaying()) {
                                ++log.replayed_steps;
                            }
                        }
                        world.get<Body>(entity).x += static_cast<float>(move.dx);
                    },
            .collect =
                    [](ecs::World& world, ecs::Entity) {
                        Log& log = world.ctx<Log>();
                        const Tick tick = world.ctx<NetClient>().predicted_tick();
                        std::int32_t dx = 0;
                        if (log.input != nullptr) {
                            dx = static_cast<std::int32_t>(std::lround(log.input->axis(log.move_x)));
                        } else if (log.script) {
                            dx = log.script(tick);
                        }
                        log.collected[tick] = dx;
                        return Move{dx};
                    },
            .capture = [](const ecs::World& world, ecs::Entity entity) { return world.get<Body>(entity); },
            .restore = [](ecs::World& world, ecs::Entity entity, const Body& body) { world.get<Body>(entity) = body; },
            .corrected =
                    [](ecs::World& world, ecs::Entity, const Body& before, const Body& after) {
                        Log& log = world.ctx<Log>();
                        ++log.corrections;
                        log.last_correction = after.x - before.x;
                    },
    };
}

void register_body(ecs::World& world) {
    register_replicated<Body>(
            world, "Body", [](ReplicaWriter& out, const Body& body) { out.bytes().write_float(body.x); },
            [](ReplicaReader& in, Body& body) { return in.bytes().read(body.x); });
}

// Body and Move on both sides; the client's input is `script(tick)`.
void setup(NetHarness& net, std::function<std::int32_t(Tick)> script) {
    register_body(net.server);
    register_body(net.client);
    register_prediction(net.server, move_desc());
    register_prediction(net.client, move_desc());
    net.client.ctx<Log>().script = std::move(script);
}

void set_latency(NetHarness& net, double ms) {
    net.server_link.set_latency_ms(ms);
    net.client_link.set_latency_ms(ms);
}

[[nodiscard]] PeerId client_peer(NetHarness& net) {
    const auto peers = net.server.ctx<NetServer>().peers();
    EXPECT_EQ(peers.size(), 1u);
    return peers.empty() ? kInvalidPeerId : peers.front();
}

// A server entity with a Body, owned by `owner`.
NetId spawn_body(NetHarness& net, PeerId owner) {
    const ecs::Entity entity = net.server.create();
    net.server.emplace<Transform>(entity);
    net.server.emplace<Body>(entity);
    return replicate(net.server, entity, owner);
}

// Connects, waits for the peer, and spawns one entity it owns. Steps until the client predicts it.
NetId connect_owned(NetHarness& net, int warmup = 60) {
    net.connect();
    net.step(2);
    const NetId id = spawn_body(net, client_peer(net));
    net.step(warmup);
    EXPECT_TRUE(net.client.ctx<NetClient>().is_predicted(id));
    return id;
}

[[nodiscard]] float server_x(NetHarness& net, NetId id) {
    return net.server.get<Body>(net.server_entity(id)).x;
}

[[nodiscard]] float client_x(NetHarness& net, NetId id) {
    const auto entity = net.client_entity(id);
    EXPECT_TRUE(entity.has_value());
    return entity ? net.client.get<Body>(*entity).x : -1.0f;
}

// The client is at its predicted tick; the server is behind. With every input applied on time, the client's x
// is the server's plus the inputs the server has not run yet.
void expect_converged(NetHarness& net, NetId id) {
    const Tick server_tick = net.tick();
    const Tick predicted = net.client.ctx<NetClient>().predicted_tick();
    ASSERT_GT(predicted, server_tick);
    const Log& log = net.client.ctx<Log>();
    float expected = server_x(net, id);
    for (Tick tick = server_tick + 1; tick <= predicted; ++tick) {
        ASSERT_TRUE(log.collected.contains(tick)) << tick;
        expected += static_cast<float>(log.collected.at(tick));
    }
    EXPECT_EQ(client_x(net, id), expected);
}

[[nodiscard]] NetInputStats input_stats(NetHarness& net) {
    return net.server.ctx<NetServer>().input_stats(client_peer(net)).value_or(NetInputStats{});
}

[[nodiscard]] std::int32_t varied(Tick tick) {
    return static_cast<std::int32_t>(tick % 5) - 2;
}

[[nodiscard]] bool is_input(std::span<const std::uint8_t> bytes) {
    return testing::message_id(bytes) == kInputMessageId;
}

[[nodiscard]] std::size_t inputs_sent(const testing::SimulatedLink& link) {
    std::size_t count = 0;
    for (const auto& sent : link.sent()) {
        count += is_input(sent.bytes) ? 1 : 0;
    }
    return count;
}

// An Input message for one entity, `inputs` newest first.
std::vector<std::uint8_t> input_message(Tick newest, NetId id, const std::vector<std::int32_t>& inputs) {
    ByteWriter out;
    out.write_u16(kInputMessageId);
    out.write_u32(newest);
    out.write_u8(1);
    out.write_u32(id);
    out.write_u8(static_cast<std::uint8_t>(inputs.size()));
    for (const std::int32_t dx : inputs) {
        out.write_u16(4);
        out.write_i32(dx);
    }
    return out.buffer();
}

TEST(NetPrediction, InputsAppliedOncePerTickInOrder) {
    NetHarness net(9301);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net);
    net.step(300);

    const Log& server = net.server.ctx<Log>();
    const Log& client = net.client.ctx<Log>();
    ASSERT_GT(server.server_steps.size(), 300u);
    const Tick first = server.server_steps.front().first;
    for (std::size_t i = 0; i < server.server_steps.size(); ++i) {
        ASSERT_EQ(server.server_steps[i].first, first + i) << "one step per tick, in order";
    }
    // From the first tick the client predicted, the server ran exactly the input the client collected.
    ASSERT_FALSE(client.collected.empty());
    const Tick first_predicted = client.collected.begin()->first;
    std::size_t compared = 0;
    for (const auto& [tick, dx] : server.server_steps) {
        if (tick >= first_predicted) {
            ASSERT_TRUE(client.collected.contains(tick)) << tick;
            EXPECT_EQ(dx, client.collected.at(tick)) << tick;
            ++compared;
        }
    }
    EXPECT_GT(compared, 300u);
    const NetInputStats stats = input_stats(net);
    EXPECT_GT(stats.applied, 300u);
    EXPECT_EQ(stats.repeated, 0u);
    EXPECT_EQ(stats.late, 0u);
    EXPECT_GT(stats.duplicate, stats.applied) << "every input travels in several messages";
    EXPECT_EQ(stats.rejected, 0u);
    EXPECT_NE(net.client.ctx<NetClient>().server_input_tick(), kNoTick);
    EXPECT_LE(net.client.ctx<NetClient>().server_input_tick(), net.client.ctx<NetClient>().newest_tick());
    expect_converged(net, id);
}

TEST(NetPrediction, NoReplayWhenPredictionsMatch) {
    NetHarness net(9302);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net, 120);
    const PredictionStats before = net.client.ctx<NetClient>().prediction_stats();
    const int steps_before = net.client.ctx<Log>().client_steps;
    const int replayed_before = net.client.ctx<Log>().replayed_steps;
    const Tick predicted_before = net.client.ctx<NetClient>().predicted_tick();
    net.step(300);

    const PredictionStats after = net.client.ctx<NetClient>().prediction_stats();
    EXPECT_EQ(after.replays, before.replays);
    EXPECT_EQ(after.snaps, before.snaps);
    EXPECT_GE(after.matched - before.matched, 140u);
    // Only the live steps ran: one per predicted tick.
    EXPECT_EQ(static_cast<Tick>(net.client.ctx<Log>().client_steps - steps_before),
            net.client.ctx<NetClient>().predicted_tick() - predicted_before);
    EXPECT_EQ(net.client.ctx<Log>().replayed_steps, replayed_before);
    expect_converged(net, id);
}

TEST(NetPrediction, ServerCorrectionReplaysOnce) {
    NetHarness net(9303);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net, 120);
    const PredictionStats before = net.client.ctx<NetClient>().prediction_stats();
    const int replayed_before = net.client.ctx<Log>().replayed_steps;

    // The server moves the entity outside `step`, which the client cannot predict.
    const Tick at = net.tick() + 10;
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [id, at](ecs::World& world) {
        if (world.ctx<NetServer>().tick() == at) {
            world.get<Body>(*find_entity(world, id)).x += 100.0f;
        }
    });
    net.step(60);

    const PredictionStats after = net.client.ctx<NetClient>().prediction_stats();
    EXPECT_EQ(after.replays, before.replays + 1);
    EXPECT_EQ(after.snaps, before.snaps);
    const Log& log = net.client.ctx<Log>();
    EXPECT_EQ(static_cast<std::uint64_t>(log.replayed_steps - replayed_before), after.replayed_ticks - before.replayed_ticks);
    EXPECT_GT(log.replayed_steps, replayed_before);
    EXPECT_EQ(log.last_correction, 100.0f);
    expect_converged(net, id);
}

TEST(NetPrediction, ReconciliationConvergesUnderLossAndJitter) {
    NetHarness net(9304);
    setup(net, varied);
    set_latency(net, 50.0);
    net.server_link.set_jitter_ms(30, 7);
    net.client_link.set_jitter_ms(30, 8);
    std::minstd_rand server_random(11);
    std::minstd_rand client_random(12);
    net.server_link.set_drop([&server_random](std::size_t, std::span<const std::uint8_t>) {
        return server_random() % 5 == 0;
    });
    net.client_link.set_drop([&client_random](std::size_t, std::span<const std::uint8_t>) {
        return client_random() % 5 == 0;
    });
    const NetId id = connect_owned(net, 120);
    net.step(600);
    const PredictionStats soaked = net.client.ctx<NetClient>().prediction_stats();
    EXPECT_GT(soaked.replays, 0u);
    EXPECT_GT(soaked.matched, 0u);

    // A clean link again: the prediction settles and stops replaying.
    net.server_link.set_jitter_ms(0);
    net.client_link.set_jitter_ms(0);
    net.server_link.set_drop({});
    net.client_link.set_drop({});
    net.step(240);
    const PredictionStats settled = net.client.ctx<NetClient>().prediction_stats();
    net.step(60);
    const PredictionStats last = net.client.ctx<NetClient>().prediction_stats();
    EXPECT_EQ(last.replays, settled.replays);
    EXPECT_EQ(last.snaps, settled.snaps);
    EXPECT_GT(last.matched, settled.matched);
    expect_converged(net, id);
}

TEST(NetPrediction, MissingInputRepeatsTheLastOne) {
    NetClientConfig config;
    config.input_redundancy = 1;
    NetHarness net(9305, {}, config);
    setup(net, [](Tick tick) { return static_cast<std::int32_t>(tick % 7); });
    set_latency(net, 50.0);
    std::size_t seen = 0;
    std::vector<Tick> dropped;
    net.client_link.set_drop([&seen, &dropped](std::size_t, std::span<const std::uint8_t> bytes) {
        if (!is_input(bytes)) {
            return false;
        }
        ++seen;
        if (seen > 200 && seen <= 205) {
            ByteReader reader(bytes);
            (void)reader.read_u16();
            dropped.push_back(*reader.read_u32());
            return true;
        }
        return false;
    });
    const NetId id = connect_owned(net, 120);
    const NetInputStats before = input_stats(net);
    net.step(200);
    const NetInputStats after = input_stats(net);

    ASSERT_EQ(dropped.size(), 5u);
    EXPECT_EQ(after.repeated - before.repeated, 5u);
    const Log& server = net.server.ctx<Log>();
    const std::int32_t last_real = net.client.ctx<Log>().collected.at(dropped.front() - 1);
    for (const auto& [tick, dx] : server.server_steps) {
        if (tick >= dropped.front() && tick <= dropped.back()) {
            EXPECT_EQ(dx, last_real) << tick;
        }
    }
    EXPECT_GT(net.client.ctx<NetClient>().prediction_stats().replays, 0u);
    expect_converged(net, id);
}

TEST(NetPrediction, LateInputIsNeverApplied) {
    NetHarness net(9306);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net, 120);
    net.client_link.hold();
    net.step(12);
    net.client_link.release(true);
    net.step(240);

    const NetInputStats stats = input_stats(net);
    EXPECT_GT(stats.late, 0u);
    EXPECT_GT(stats.repeated, 0u);
    const Log& server = net.server.ctx<Log>();
    const Tick first = server.server_steps.front().first;
    for (std::size_t i = 0; i < server.server_steps.size(); ++i) {
        ASSERT_EQ(server.server_steps[i].first, first + i);
    }
    expect_converged(net, id);
}

TEST(NetPrediction, RejectsInputForEntityNotOwned) {
    NetHarness net(9307);
    setup(net, varied);
    const NetId own = connect_owned(net);
    const NetId server_owned = spawn_body(net, kServerPeerId);
    const NetId someone_else = spawn_body(net, 77);
    net.step(2);

    const Tick tick = net.tick() + 3;
    net.client_link.send(kServerPeerId, input_message(tick, server_owned, {50}), DeliveryMode::UnreliableImmediate);
    net.client_link.send(kServerPeerId, input_message(tick, someone_else, {50}), DeliveryMode::UnreliableImmediate);
    net.client_link.send(kServerPeerId, input_message(tick, 999, {50, 50}), DeliveryMode::UnreliableImmediate);
    net.step(10);

    EXPECT_EQ(input_stats(net).rejected, 4u);
    EXPECT_EQ(server_x(net, server_owned), 0.0f);
    EXPECT_EQ(server_x(net, someone_else), 0.0f);
    for (const auto& [step_tick, dx] : net.server.ctx<Log>().server_steps) {
        EXPECT_NE(dx, 50) << step_tick;
    }
    expect_converged(net, own);
}

TEST(NetPrediction, RejectsInputTooFarAheadAndMalformed) {
    NetHarness net(9308);
    setup(net, varied);
    const NetId id = connect_owned(net);

    const Tick far = net.tick() + kMaxInputLeadTicks + 5;
    net.client_link.send(kServerPeerId, input_message(far, id, {77}), DeliveryMode::UnreliableImmediate);
    auto truncated = input_message(net.tick() + 3, id, {77});
    truncated.pop_back();
    net.client_link.send(kServerPeerId, truncated, DeliveryMode::UnreliableImmediate);
    auto trailing = input_message(net.tick() + 3, id, {77});
    trailing[2 + 4 + 1 + 4 + 1] = 5; // The input claims 5 bytes; Move decodes 4.
    trailing.push_back(0);
    net.client_link.send(kServerPeerId, trailing, DeliveryMode::UnreliableImmediate);
    net.step(100);

    const NetInputStats stats = input_stats(net);
    EXPECT_EQ(stats.too_far_ahead, 1u);
    EXPECT_EQ(stats.malformed, 2u);
    for (const auto& [tick, dx] : net.server.ctx<Log>().server_steps) {
        EXPECT_NE(dx, 77) << tick;
    }
    expect_converged(net, id);
}

TEST(NetPrediction, OwnedEntityIsPredictedOthersInterpolated) {
    NetHarness net(9309);
    setup(net, varied);
    const NetId own = connect_owned(net);
    const NetId other = net.spawn();
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [other](ecs::World& world) {
        world.get<Transform>(*find_entity(world, other)).position.x = static_cast<float>(world.ctx<NetServer>().tick());
    });
    net.step(60);

    const NetClient& client = net.client.ctx<NetClient>();
    EXPECT_TRUE(client.is_predicted(own));
    EXPECT_FALSE(client.is_predicted(other));
    EXPECT_GT(client.predicted_tick(), net.tick());
    EXPECT_LT(client.render_tick(), static_cast<double>(net.tick()));
    expect_converged(net, own);
    const auto other_entity = net.client_entity(other);
    ASSERT_TRUE(other_entity.has_value());
    EXPECT_NEAR(net.client.get<Transform>(*other_entity).position.x, client.render_tick(), 1e-3);
}

TEST(NetPrediction, LeadSettlesAtTheInputBuffer) {
    NetHarness net(9310);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net, 240);

    const NetClient& client = net.client.ctx<NetClient>();
    ASSERT_TRUE(client.input_slack().has_value());
    EXPECT_NEAR(*client.input_slack(), 2, 1);
    EXPECT_NEAR(net.client.ctx<Time>().fixed_rate, 1.0f, 0.02f);
    EXPECT_EQ(input_stats(net).late, 0u);

    // The link gets slower: inputs arrive later until the client runs further ahead.
    set_latency(net, 120.0);
    net.step(360);
    const NetInputStats settled = input_stats(net);
    net.step(120);
    EXPECT_NEAR(*client.input_slack(), 2, 1);
    EXPECT_NEAR(net.client.ctx<Time>().fixed_rate, 1.0f, 0.02f);
    EXPECT_EQ(input_stats(net).late, settled.late);
    EXPECT_EQ(input_stats(net).repeated, settled.repeated);
    expect_converged(net, id);
}

TEST(NetPrediction, LatencySpikeJumpsForwardAndRecovers) {
    NetHarness net(9315);
    setup(net, varied);
    set_latency(net, 50.0);
    const NetId id = connect_owned(net, 120);
    const NetClient& client = net.client.ctx<NetClient>();
    EXPECT_EQ(client.prediction_stats().snaps, 0u);

    // 15 ticks more one-way than the lead covers: far beyond what dilation closes in time.
    set_latency(net, 300.0);
    Tick previous = client.predicted_tick();
    for (int frame = 0; frame < 480; ++frame) {
        net.step();
        ASSERT_GE(client.predicted_tick(), previous) << "the predicted tick never goes back";
        previous = client.predicted_tick();
    }
    EXPECT_GT(client.prediction_stats().snaps, 0u);
    EXPECT_TRUE(client.is_predicted(id));

    const NetInputStats settled = input_stats(net);
    net.step(120);
    EXPECT_NEAR(*client.input_slack(), 2, 1);
    EXPECT_EQ(input_stats(net).late, settled.late);
    EXPECT_EQ(input_stats(net).repeated, settled.repeated);
    expect_converged(net, id);
}

TEST(NetPrediction, OwnershipChangeStartsAndStopsPrediction) {
    NetHarness net(9311);
    setup(net, varied);
    net.connect();
    net.step(2);
    const NetId id = spawn_body(net, kServerPeerId);
    net.step(30);
    const NetClient& client = net.client.ctx<NetClient>();
    EXPECT_FALSE(client.is_predicted(id));
    EXPECT_EQ(inputs_sent(net.client_link), 0u);

    net.server.get<NetIdentity>(net.server_entity(id)).owner = client_peer(net);
    net.step(30);
    EXPECT_TRUE(client.is_predicted(id));
    EXPECT_GT(inputs_sent(net.client_link), 0u);
    expect_converged(net, id);

    net.server.get<NetIdentity>(net.server_entity(id)).owner = kServerPeerId;
    net.step(10);
    EXPECT_FALSE(client.is_predicted(id));
    const std::size_t sent = inputs_sent(net.client_link);
    const std::size_t server_steps = net.server.ctx<Log>().server_steps.size();
    net.step(30);
    EXPECT_EQ(inputs_sent(net.client_link), sent);
    EXPECT_EQ(net.server.ctx<Log>().server_steps.size(), server_steps) << "a server-owned entity is not stepped";
    EXPECT_EQ(client_x(net, id), server_x(net, id)) << "interpolated again";
}

TEST(NetPrediction, DisconnectClearsPrediction) {
    NetHarness net(9312);
    setup(net, varied);
    set_latency(net, 50.0);
    (void)connect_owned(net, 120);
    net.client.ctx<Time>().fixed_rate = 1.05f;
    net.client_link.disconnect(kServerPeerId);
    net.step(2);

    const NetClient& client = net.client.ctx<NetClient>();
    EXPECT_EQ(client.predicted_tick(), kNoTick);
    EXPECT_FALSE(client.input_slack().has_value());
    EXPECT_EQ(client.prediction_stats().matched, 0u);
    EXPECT_EQ(client.prediction_stats().replays, 0u);
    EXPECT_FLOAT_EQ(net.client.ctx<Time>().fixed_rate, 1.0f);
}

TEST(NetPrediction, SchemaMismatchWhenOnlyTheServerPredicts) {
    NetHarness net(9313);
    register_body(net.server);
    register_body(net.client);
    register_prediction(net.server, move_desc());
    net.connect();
    net.step(10);
    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed());
    EXPECT_TRUE(net.server.ctx<NetServer>().peers().empty());
}

TEST(NetPrediction, CollectsInputThroughActions) {
    NetHarness net(9314);
    setup(net, {});
    InputSystem input;
    input.set_router([&net](WindowId) { return &net.client; });
    const ActionId move_x = input.intern("move_x");
    input.bind(KeyCode::D, move_x, 1.0f);
    input.bind(KeyCode::A, move_x, -1.0f);
    net.client.ctx<Log>().input = &input;
    net.client.ctx<Log>().move_x = move_x;
    const NetId id = connect_owned(net);
    EXPECT_EQ(server_x(net, id), 0.0f);

    input.handle_key(KeyCode::D, true);
    net.step(60);
    input.handle_key(KeyCode::D, false);
    input.handle_key(KeyCode::A, true);
    net.step(20);
    input.handle_key(KeyCode::A, false);
    net.step(30);

    // Every tick D was held moved right, every tick A was held moved left, and each reached the server.
    const Log& log = net.client.ctx<Log>();
    int right = 0;
    int left = 0;
    float sum = 0.0f;
    for (const auto& [tick, dx] : log.collected) {
        right += dx == 1 ? 1 : 0;
        left += dx == -1 ? 1 : 0;
        if (tick <= net.tick()) {
            sum += static_cast<float>(dx);
        }
    }
    EXPECT_GE(right, 55);
    EXPECT_GE(left, 15);
    EXPECT_EQ(server_x(net, id), sum);
    EXPECT_EQ(server_x(net, id), static_cast<float>(right - left));
    EXPECT_EQ(input_stats(net).repeated, 0u);
    expect_converged(net, id);
}

TEST(NetPrediction, ClientBehindTheServerPredictsAfterTheSnapshot) {
    NetHarness net(9316);
    setup(net, [](Tick) { return 1; });
    (void)connect_owned(net, 120);

    // The client stalls while the server runs on.
    for (int frame = 0; frame < 120; ++frame) {
        net.server.flush_events();
        net.server.ctx<Time>().fixed_delta_time = kFixed;
        net.server.run(ecs::Schedule::Fixed);
        net.server_link.advance(1000.0 / 60.0);
        net.client_link.advance(1000.0 / 60.0);
    }
    // A frame receives the snapshots; the next fixed step predicts.
    net.client.flush_events();
    net.client.run(ecs::Schedule::Frame);
    net.client.run(ecs::Schedule::Fixed);

    // Past the newest snapshot, which is at most one snapshot interval behind the server: no tick is stepped twice.
    EXPECT_GE(net.client.ctx<NetClient>().predicted_tick(), net.tick() - 2);
}

} // namespace
} // namespace engine::net
