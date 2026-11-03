#include <gtest/gtest.h>

#include "net/snapshot_clock.h"

namespace engine::net {
namespace {

TEST(SnapshotClock, StartsAtFirstObservationAndRunsWithRealTime) {
    SnapshotClock clock;
    EXPECT_FALSE(clock.started());
    clock.advance(1.0);
    EXPECT_FALSE(clock.started());
    clock.observe(5.0);
    EXPECT_TRUE(clock.started());
    EXPECT_DOUBLE_EQ(clock.now(), 5.0);
    clock.advance(0.5);
    EXPECT_DOUBLE_EQ(clock.now(), 5.5);
}

TEST(SnapshotClock, ConvergesWithBoundedRate) {
    SnapshotClock clock;
    clock.observe(0.0);
    constexpr double dt = 1.0 / 60.0;
    double real = 0.0;
    // Snapshots turn out to be 0.1 s later than the first one suggested (it arrived early).
    for (int frame = 1; frame <= 60 * 10; ++frame) {
        real += dt;
        if (frame % 2 == 0) {
            clock.observe(real + 0.1);
        }
        const double before = clock.now();
        clock.advance(dt);
        const double step = clock.now() - before;
        EXPECT_GE(step, dt * (1.0 - SnapshotClock::kMaxRateAdjust) - 1e-12);
        EXPECT_LE(step, dt * (1.0 + SnapshotClock::kMaxRateAdjust) + 1e-12);
    }
    // The last observation was followed by one more advance.
    EXPECT_NEAR(clock.now(), real + 0.1 + dt, 0.005);
}

TEST(SnapshotClock, SnapsOnLargeError) {
    SnapshotClock clock;
    clock.observe(1.0);
    clock.observe(1.0 + SnapshotClock::kSnapSeconds * 2.0);
    EXPECT_DOUBLE_EQ(clock.now(), 1.0 + SnapshotClock::kSnapSeconds * 2.0);
    clock.reset();
    EXPECT_FALSE(clock.started());
}

TEST(SnapshotClock, MonotonicUnderJitter) {
    SnapshotClock clock;
    constexpr double dt = 1.0 / 60.0;
    const double jitter[] = {0.0, 0.02, -0.015, 0.01, -0.02, 0.018, -0.005};
    double real = 0.0;
    double last = -1.0;
    for (int frame = 0; frame < 600; ++frame) {
        real += dt;
        if (frame % 2 == 0) {
            clock.observe(real - 0.05 + jitter[frame % 7]);
        }
        clock.advance(dt);
        if (clock.started()) {
            EXPECT_GT(clock.now(), last);
            last = clock.now();
        }
    }
    EXPECT_NEAR(clock.now(), real - 0.05 + dt, 0.03);
}

} // namespace
} // namespace engine::net
