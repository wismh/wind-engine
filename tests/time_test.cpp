#include <gtest/gtest.h>

#include <engine/core/application_state.h>
#include <engine/core/fixed_step.h>
#include <engine/core/time.h>

TEST(Time, OneFixedStep) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(engine::FIXED);

    EXPECT_EQ(steps, 1);
    EXPECT_FLOAT_EQ(time.fixedDeltaTime, engine::FIXED);
    EXPECT_FLOAT_EQ(time.deltaTime, engine::FIXED);
}

TEST(Time, TwoFixedSteps) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(2.0f * engine::FIXED);

    EXPECT_EQ(steps, 2);
    EXPECT_FLOAT_EQ(time.fixedDeltaTime, engine::FIXED);
}

TEST(Time, CapMaxFixedSteps) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const int steps = clock.advance(9.0f * engine::FIXED);

    EXPECT_EQ(steps, engine::MAX_FIXED_STEPS);
    EXPECT_EQ(steps, 8);
    EXPECT_FLOAT_EQ(time.accumulator, 0.0f);
    EXPECT_LT(time.accumulator, engine::FIXED);
}

TEST(Time, PausedZeroStepsAccumulatorFrozen) {
    engine::Time time;
    engine::ApplicationState app;
    engine::FixedStepClock clock{time, app};

    const float remainder = 0.5f * engine::FIXED;
    time.accumulator = remainder;
    app.paused = true;

    const int paused_steps = clock.advance(1.0f);

    EXPECT_EQ(paused_steps, 0);
    EXPECT_FLOAT_EQ(time.accumulator, remainder);

    app.paused = false;
    const int unpaused_steps = clock.advance(engine::FIXED);

    EXPECT_EQ(unpaused_steps, 1);
    EXPECT_NE(unpaused_steps, engine::MAX_FIXED_STEPS);
    EXPECT_FLOAT_EQ(time.accumulator, remainder);
}
