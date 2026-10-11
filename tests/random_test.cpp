#include <gtest/gtest.h>

#include <engine/core/random.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint32_t> draw(engine::Random& random, int count) {
    std::vector<std::uint32_t> values;
    for (int i = 0; i < count; ++i) {
        values.push_back(random.next_u32());
    }
    return values;
}

}

TEST(Random, MatchesTheReferencePcg32Output) {
    // The PCG32 reference demo: seed 42, stream 54.
    engine::Random random(42, 54);
    EXPECT_EQ(random.next_u32(), 0xa15c02b7u);
    EXPECT_EQ(random.next_u32(), 0x7b47f409u);
    EXPECT_EQ(random.next_u32(), 0xba1d3330u);
    EXPECT_EQ(random.next_u32(), 0x83d2f293u);
    EXPECT_EQ(random.next_u32(), 0xbfa4784bu);
    EXPECT_EQ(random.next_u32(), 0xcbed606eu);
}

TEST(Random, GoldenSequenceOfSeedOne) {
    engine::Random random(1);
    const std::vector<std::uint32_t> expected{
            3795398737u, 17903413u, 3545275701u, 194195274u, 2326030198u, 2354257974u, 2697798104u, 3102124240u};
    EXPECT_EQ(draw(random, 8), expected);
}

TEST(Random, GoldenSequenceOfSeed12345) {
    engine::Random random(12345);
    const std::vector<std::uint32_t> expected{
            304133009u, 2564000426u, 1539170214u, 2267019874u, 321857903u, 29877282u, 4241239986u, 528810775u};
    EXPECT_EQ(draw(random, 8), expected);
}

TEST(Random, GoldenMappings) {
    engine::Random dice(1);
    const std::array<int, 10> rolls{2, 2, 4, 1, 5, 1, 3, 5, 6, 3};
    for (const int roll : rolls) {
        EXPECT_EQ(dice.uniform(1, 6), roll);
    }

    engine::Random signed_range(1);
    const std::array<int, 8> offsets{1, 0, -1, -3, -1, 2, -2, 2};
    for (const int value : offsets) {
        EXPECT_EQ(signed_range.uniform(-3, 3), value);
    }

    engine::Random floats(1);
    EXPECT_FLOAT_EQ(floats.next_float(), 0.88368511f);
    EXPECT_FLOAT_EQ(floats.next_float(), 0.00416845f);
    EXPECT_FLOAT_EQ(floats.next_float(), 0.82544881f);
}

TEST(Random, SameSeedSameSequenceAndReseedRestarts) {
    engine::Random a(7);
    engine::Random b(7);
    EXPECT_EQ(draw(a, 32), draw(b, 32));

    a.seed(7);
    engine::Random c(7);
    EXPECT_EQ(draw(a, 32), draw(c, 32));
}

TEST(Random, DifferentSeedsAndStreamsDiffer) {
    engine::Random a(1);
    engine::Random b(2);
    engine::Random c(1, 1);
    const auto from_a = draw(a, 16);
    EXPECT_NE(from_a, draw(b, 16));
    EXPECT_NE(from_a, draw(c, 16));
}

TEST(Random, UniformIntStaysInRangeAndHitsEveryValue) {
    engine::Random random(99);
    std::array<int, 7> counts{};
    for (int i = 0; i < 70000; ++i) {
        const int value = random.uniform(-3, 3);
        ASSERT_GE(value, -3);
        ASSERT_LE(value, 3);
        ++counts[static_cast<std::size_t>(value + 3)];
    }
    for (const int count : counts) {
        // Expected 10000 each; a biased or stuck generator is far outside this band.
        EXPECT_GT(count, 9300);
        EXPECT_LT(count, 10700);
    }
}

TEST(Random, UniformIntDegenerateAndExtremeRanges) {
    engine::Random random(5);
    EXPECT_EQ(random.uniform(4, 4), 4);
    for (int i = 0; i < 100; ++i) {
        (void)random.uniform(INT32_MIN, INT32_MAX);
        const int value = random.uniform(INT32_MAX - 1, INT32_MAX);
        EXPECT_GE(value, INT32_MAX - 1);
    }
}

TEST(Random, FloatsStayInRangeWithAFairMean) {
    engine::Random random(3);
    double sum = 0.0;
    for (int i = 0; i < 20000; ++i) {
        const float unit = random.next_float();
        ASSERT_GE(unit, 0.0f);
        ASSERT_LT(unit, 1.0f);
        const float ranged = random.uniform(-2.0f, 6.0f);
        ASSERT_GE(ranged, -2.0f);
        ASSERT_LT(ranged, 6.0f);
        sum += unit;
    }
    EXPECT_NEAR(sum / 20000.0, 0.5, 0.01);
}

TEST(Random, ChanceFollowsTheProbability) {
    engine::Random random(11);
    int hits = 0;
    for (int i = 0; i < 20000; ++i) {
        hits += random.chance(0.25f) ? 1 : 0;
    }
    EXPECT_NEAR(hits / 20000.0, 0.25, 0.015);
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(random.chance(0.0f));
        EXPECT_FALSE(random.chance(-1.0f));
        EXPECT_TRUE(random.chance(1.0f));
        EXPECT_TRUE(random.chance(7.0f));
    }
}

TEST(Random, PickTakesAnElementOfAnyRandomAccessRange) {
    engine::Random random(21);
    const std::array<int, 3> values{10, 20, 30};
    std::array<int, 3> seen{};
    for (int i = 0; i < 300; ++i) {
        const int value = random.pick(values);
        ASSERT_TRUE(value == 10 || value == 20 || value == 30);
        ++seen[static_cast<std::size_t>(value / 10 - 1)];
    }
    for (const int count : seen) {
        EXPECT_GT(count, 0);
    }
    std::vector<int> single{5};
    EXPECT_EQ(random.pick(single), 5);
}

TEST(Random, ClockSeededInstancesDiffer) {
    engine::Random a;
    engine::Random b;
    // Two clock seeds can in theory collide; sixteen draws agreeing is not a realistic outcome.
    EXPECT_NE(draw(a, 16), draw(b, 16));
}

TEST(Random, WorldHoldsOneAndItsSeedIsReplayable) {
    engine::ecs::World first;
    engine::ecs::World second;
    first.ctx<engine::Random>().seed(2024);
    second.ctx<engine::Random>().seed(2024);

    EXPECT_EQ(draw(first.ctx<engine::Random>(), 8), draw(second.ctx<engine::Random>(), 8));
    EXPECT_EQ(&first.ctx<engine::Random>(), &first.ctx<engine::Random>());
}

namespace {

class UnseededGame final : public engine::IGame {
public:
    engine::ecs::World& world() override { return world_; }
    void on_start() override {}
    void on_quit() override {}

private:
    engine::ecs::World world_;
};

class SeededGame final : public engine::IGame {
public:
    std::optional<std::uint64_t> random_seed() const override { return 77; }
    engine::ecs::World& world() override { return world_; }
    void on_start() override {}
    void on_quit() override {}

private:
    engine::ecs::World world_;
};

}

TEST(Random, GameSeedIsUnsetByDefault) {
    UnseededGame unseeded;
    SeededGame seeded;
    EXPECT_FALSE(unseeded.random_seed().has_value());
    ASSERT_TRUE(seeded.random_seed().has_value());
    EXPECT_EQ(*seeded.random_seed(), 77u);
}
