#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_prediction.h>
#include <engine/net/prediction.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace engine::net {
namespace {

struct Move {
    std::int32_t dx{0};
};

struct Body {
    float x{0.0f};
    bool operator==(const Body&) const = default;
};

// What the test's hooks saw.
struct Probe {
    int steps{0};
    int replayed_steps{0};
    bool* replaying{nullptr};
    std::optional<std::pair<Body, Body>> corrected{};
    // The input `collect` returns for the next predicted tick.
    std::int32_t next_dx{1};
};

constexpr NetId kId = 7;

PredictionDesc<Move, Body> body_desc() {
    return PredictionDesc<Move, Body>{
            .name = "Move",
            .write_input = [](ByteWriter& out, const Move& move) { out.write_i32(move.dx); },
            .read_input = [](ByteReader& in, Move& move) { return in.read(move.dx); },
            .step =
                    [](ecs::World& world, ecs::Entity entity, const Move& move) {
                        auto& probe = world.ctx<Probe>();
                        ++probe.steps;
                        if (probe.replaying != nullptr && *probe.replaying) {
                            ++probe.replayed_steps;
                        }
                        world.get<Body>(entity).x += static_cast<float>(move.dx);
                    },
            .collect = [](ecs::World& world, ecs::Entity) { return Move{world.ctx<Probe>().next_dx}; },
            .capture = [](const ecs::World& world, ecs::Entity entity) { return world.get<Body>(entity); },
            .restore = [](ecs::World& world, ecs::Entity entity, const Body& body) { world.get<Body>(entity) = body; },
            .corrected =
                    [](ecs::World& world, ecs::Entity, const Body& before, const Body& after) {
                        world.ctx<Probe>().corrected = std::make_pair(before, after);
                    },
    };
}

class PredictionTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_prediction(world, body_desc());
        channel = world.ctx<NetPrediction>().channel.get();
        entity = world.create();
        world.emplace<Body>(entity);
        world.ctx<Probe>().replaying = &replaying;
        channel->begin(kId, entity);
    }

    // Predicts ticks `from`..`to` with dx = tick, recording each state.
    void predict(Tick from, Tick to) {
        for (Tick tick = from; tick <= to; ++tick) {
            world.ctx<Probe>().next_dx = static_cast<std::int32_t>(tick);
            channel->predict(world, kId, tick);
            channel->record(world, kId, tick);
        }
    }

    PredictionChannel::Outcome reconcile(Tick authoritative, Tick predicted, float server_x) {
        applied = false;
        return channel->reconcile(
                world, kId, authoritative, predicted,
                [this, server_x] {
                    applied = true;
                    world.get<Body>(entity).x = server_x;
                },
                replaying);
    }

    [[nodiscard]] float x() {
        return world.get<Body>(entity).x;
    }

    [[nodiscard]] Probe& probe() {
        return world.ctx<Probe>();
    }

    ecs::World world;
    PredictionChannel* channel{nullptr};
    ecs::Entity entity{};
    bool replaying{false};
    bool applied{false};
};

TEST_F(PredictionTest, MatchKeepsPresentWithoutStepping) {
    predict(1, 5); // x = 1 + 2 + 3 + 4 + 5
    ASSERT_EQ(x(), 15.0f);
    const int steps = probe().steps;

    EXPECT_EQ(reconcile(3, 5, 6.0f), PredictionChannel::Outcome::Matched);
    EXPECT_TRUE(applied);
    EXPECT_EQ(probe().steps, steps);
    EXPECT_EQ(x(), 15.0f);
    EXPECT_FALSE(probe().corrected.has_value());
}

TEST_F(PredictionTest, MismatchReplaysStoredInputs) {
    predict(1, 5);
    const int steps = probe().steps;

    EXPECT_EQ(reconcile(3, 5, 106.0f), PredictionChannel::Outcome::Replayed);
    EXPECT_EQ(probe().steps, steps + 2);
    EXPECT_EQ(probe().replayed_steps, 2);
    EXPECT_FALSE(replaying);
    EXPECT_EQ(x(), 115.0f);
    ASSERT_TRUE(probe().corrected.has_value());
    EXPECT_EQ(probe().corrected->first.x, 15.0f);
    EXPECT_EQ(probe().corrected->second.x, 115.0f);

    // The replayed states are now the predictions: the next server state matches them.
    EXPECT_EQ(reconcile(4, 5, 110.0f), PredictionChannel::Outcome::Matched);
    EXPECT_EQ(x(), 115.0f);
}

TEST_F(PredictionTest, NoStoredPredictionReplays) {
    predict(4, 5);
    EXPECT_EQ(reconcile(2, 5, 1.0f), PredictionChannel::Outcome::Replayed);
    // Tick 3 had no input and nothing before it: Input{}. Ticks 4 and 5 replay theirs.
    EXPECT_EQ(x(), 1.0f + 0.0f + 4.0f + 5.0f);
}

TEST_F(PredictionTest, MissingInputsRepeatTheLastOne) {
    predict(1, 3);
    EXPECT_EQ(reconcile(3, 6, 100.0f), PredictionChannel::Outcome::Replayed);
    // Ticks 4, 5 and 6 were never predicted: each repeats tick 3's input.
    EXPECT_EQ(x(), 109.0f);
    // Those stand-ins are never sent; the server repeats on its own.
    EXPECT_TRUE(channel->inputs(kId, 6, 8).empty());
    EXPECT_EQ(channel->inputs(kId, 3, 8).size(), 3u);
}

TEST_F(PredictionTest, ServerStateAheadIsAccepted) {
    predict(1, 2);
    const int steps = probe().steps;
    EXPECT_EQ(reconcile(5, 2, 50.0f), PredictionChannel::Outcome::Accepted);
    EXPECT_EQ(probe().steps, steps);
    EXPECT_EQ(x(), 50.0f);
}

TEST_F(PredictionTest, TooOldToReplaySnaps) {
    predict(1, 2);
    const int steps = probe().steps;
    EXPECT_EQ(reconcile(2, 2 + kPredictionRingTicks, 42.0f), PredictionChannel::Outcome::Snapped);
    EXPECT_EQ(probe().steps, steps);
    EXPECT_EQ(x(), 42.0f);
}

TEST_F(PredictionTest, OldOrRepeatedServerStateIsSkipped) {
    predict(1, 5);
    EXPECT_EQ(reconcile(4, 5, 10.0f), PredictionChannel::Outcome::Matched);
    EXPECT_EQ(reconcile(4, 5, 99.0f), PredictionChannel::Outcome::Skipped);
    EXPECT_FALSE(applied);
    EXPECT_EQ(reconcile(3, 5, 99.0f), PredictionChannel::Outcome::Skipped);
    EXPECT_EQ(x(), 15.0f);
}

TEST_F(PredictionTest, InputsNewestFirstStoppingAtAGap) {
    predict(1, 2);
    predict(4, 6);
    const auto decode = [](const std::vector<std::uint8_t>& bytes) {
        ByteReader reader(bytes);
        return *reader.read_i32();
    };
    const auto three = channel->inputs(kId, 6, 3);
    ASSERT_EQ(three.size(), 3u);
    EXPECT_EQ(decode(three[0]), 6);
    EXPECT_EQ(decode(three[1]), 5);
    EXPECT_EQ(decode(three[2]), 4);
    EXPECT_EQ(channel->inputs(kId, 6, 32).size(), 3u);
    EXPECT_TRUE(channel->inputs(kId + 1, 6, 32).empty());
}

TEST_F(PredictionTest, BeginWithAnotherEntityStartsOver) {
    predict(1, 3);
    channel->begin(kId, entity);
    EXPECT_EQ(channel->inputs(kId, 3, 8).size(), 3u);

    const ecs::Entity other = world.create();
    world.emplace<Body>(other);
    channel->begin(kId, other);
    EXPECT_TRUE(channel->inputs(kId, 3, 8).empty());

    channel->forget(kId);
    EXPECT_EQ(reconcile(3, 3, 1.0f), PredictionChannel::Outcome::Skipped);
}

TEST_F(PredictionTest, ServerSideDecodesExactlyOneInput) {
    ByteWriter good;
    good.write_i32(4);
    EXPECT_TRUE(channel->valid_input(good.bytes()));
    ByteWriter trailing;
    trailing.write_i32(4);
    trailing.write_u8(0);
    EXPECT_FALSE(channel->valid_input(trailing.bytes()));
    EXPECT_FALSE(channel->valid_input(std::span(good.bytes()).first(2)));

    channel->step_encoded(world, entity, good.bytes());
    EXPECT_EQ(x(), 4.0f);
    channel->step_encoded(world, entity, {});
    EXPECT_EQ(x(), 4.0f);
    EXPECT_EQ(probe().steps, 2);
}

} // namespace
} // namespace engine::net
