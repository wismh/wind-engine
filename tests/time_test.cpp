#include <gtest/gtest.h>

#include <engine/core/application_state.h>
#include <engine/core/fixed_step.h>
#include <engine/core/time.h>

TEST(Time, OneFixedStep) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(engine::kFixed);

    EXPECT_EQ(steps, 1);
    EXPECT_FLOAT_EQ(time.fixed_delta_time, engine::kFixed);
    EXPECT_FLOAT_EQ(time.delta_time, engine::kFixed);
}

TEST(Time, TwoFixedSteps) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(2.0f * engine::kFixed);

    EXPECT_EQ(steps, 2);
    EXPECT_FLOAT_EQ(time.fixed_delta_time, engine::kFixed);
}

TEST(Time, CapMaxFixedSteps) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(9.0f * engine::kFixed);

    EXPECT_EQ(steps, engine::kMaxFixedSteps);
    EXPECT_EQ(steps, 8);
    EXPECT_FLOAT_EQ(time.accumulator, 0.0f);
    EXPECT_LT(time.accumulator, engine::kFixed);
}

TEST(Time, PausedZeroStepsAccumulatorFrozen) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const float remainder = 0.5f * engine::kFixed;
    time.accumulator = remainder;
    app.paused = true;

    const int paused_steps = clock.advance(1.0f);

    EXPECT_EQ(paused_steps, 0);
    EXPECT_FLOAT_EQ(time.accumulator, remainder);

    app.paused = false;
    const int unpaused_steps = clock.advance(engine::kFixed);

    EXPECT_EQ(unpaused_steps, 1);
    EXPECT_NE(unpaused_steps, engine::kMaxFixedSteps);
    EXPECT_FLOAT_EQ(time.accumulator, remainder);
}
