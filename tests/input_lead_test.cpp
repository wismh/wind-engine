#include <gtest/gtest.h>

#include "net/input_lead.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace engine::net {
namespace {

TEST(InputLead, RateIsBoundedAndSigned) {
    InputLead lead;
    EXPECT_FLOAT_EQ(lead.rate(), 1.0f);

    EXPECT_EQ(lead.observe(10, -5, 2, 20), 0);
    EXPECT_GT(lead.rate(), 1.0f);
    EXPECT_LE(lead.rate(), 1.1f + 1e-6f);

    lead.reset();
    for (Tick tick = 10; tick < 100; tick += 2) {
        EXPECT_EQ(lead.observe(tick, 60, 2, 200), 0) << "never jumps backward";
    }
    EXPECT_FLOAT_EQ(lead.rate(), 0.9f);
}

TEST(InputLead, JumpsForwardWhenFarLateTwiceThenWaitsForTheJumpToShow) {
    InputLead lead;
    // One late burst only nudges the rate.
    EXPECT_EQ(lead.observe(48, -10, 2, 38), 0);
    EXPECT_GT(lead.rate(), 1.0f);
    EXPECT_EQ(lead.observe(49, 1, 2, 39), 0);
    EXPECT_EQ(lead.observe(49, -10, 2, 39), 0);
    EXPECT_EQ(lead.observe(50, -10, 2, 40), 12);
    EXPECT_FLOAT_EQ(lead.rate(), 1.0f);
    // Reports from snapshots before the new predicted tick (52) predate the jump.
    EXPECT_EQ(lead.observe(51, -10, 2, 52), 0);
    EXPECT_FLOAT_EQ(lead.rate(), 1.0f);
    EXPECT_EQ(lead.observe(52, 1, 2, 60), 0);
    EXPECT_GT(lead.rate(), 1.0f);
}

TEST(InputLead, CountsTheRateChangeTheReportCannotShowYet) {
    InputLead lead;
    for (Tick tick = 1; tick <= 20; ++tick) {
        EXPECT_EQ(lead.observe(tick, 0, 2, tick), 0);
        lead.advance(tick);
    }
    const double running = lead.error();
    EXPECT_GT(running, 0.0);

    // The same report again, but its input was sent before all those faster steps: the shortfall is smaller.
    InputLead fresh;
    for (Tick tick = 1; tick <= 20; ++tick) {
        (void)fresh.observe(tick, 0, 2, tick);
    }
    EXPECT_LT(running, fresh.error());
}

// Inputs take `one_way` ticks to arrive, and each report takes `one_way` ticks back, every second tick. The client
// starts `start_slack` ticks of slack away from the target and settles at it without swinging around it.
void expect_settles(int one_way, int start_slack) {
    constexpr int target = 2;
    constexpr int frames = 3000;
    InputLead lead;
    int server = 1000;
    int predicted = server + one_way + start_slack;
    std::vector<int> predicted_at;
    std::vector<int> server_at;
    double accumulator = 0.0;
    int worst_off = 0;
    for (int frame = 0; frame < frames; ++frame) {
        ++server;
        accumulator += static_cast<double>(lead.rate());
        while (accumulator >= 1.0) {
            accumulator -= 1.0;
            ++predicted;
            lead.advance(static_cast<Tick>(predicted));
        }
        predicted_at.push_back(predicted);
        server_at.push_back(server);
        if (frame % 2 == 0 && frame >= 2 * one_way) {
            // The input sent two trips ago reached the server one trip ago; that server tick's report is here now.
            const int sent = frame - 2 * one_way;
            const int measured = frame - one_way;
            const int slack = predicted_at[static_cast<std::size_t>(sent)] - server_at[static_cast<std::size_t>(measured)];
            predicted += lead.observe(static_cast<Tick>(server_at[static_cast<std::size_t>(measured)]), slack, target,
                    static_cast<Tick>(predicted));
        }
        if (frame > frames / 2) {
            worst_off = std::max(worst_off, std::abs(predicted - server - one_way - target));
        }
    }
    EXPECT_LE(worst_off, 1) << "one_way " << one_way;
    EXPECT_NEAR(lead.rate(), 1.0f, 0.06f) << "one_way " << one_way;
}

TEST(InputLead, SettlesThroughTheFeedbackDelay) {
    expect_settles(3, 0);
    expect_settles(3, 6);
    expect_settles(18, 0);
    expect_settles(18, 6);
    expect_settles(40, -2);
}

} // namespace
} // namespace engine::net
