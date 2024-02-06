#include <gtest/gtest.h>

#include <engine/core/application_state.h>
#include <engine/core/time.h>
#include <engine/core/web_loop.h>

TEST(WebLoop, BlockingPolicyDoesNotUseRaf) {
    engine::MainLoopPolicy policy{engine::LoopKind::Blocking};
    EXPECT_EQ(policy.kind(), engine::LoopKind::Blocking);
    EXPECT_FALSE(policy.uses_request_animation_frame());
}

TEST(WebLoop, RafPolicyUsesRaf) {
    engine::MainLoopPolicy policy{engine::LoopKind::RequestAnimationFrame};
    EXPECT_EQ(policy.kind(), engine::LoopKind::RequestAnimationFrame);
    EXPECT_TRUE(policy.uses_request_animation_frame());
}

TEST(WebLoop, DefaultPolicyMatchesPlatform) {
    engine::MainLoopPolicy policy;
    EXPECT_EQ(policy.kind(), engine::default_loop_kind());
}

TEST(WebLoop, PumpStopsOnQuit) {
    engine::ApplicationState app;
    app.running = true;
    int ticks = 0;
    engine::MainLoopPolicy policy{engine::LoopKind::RequestAnimationFrame};
    const int frames = policy.pump(
            app,
            [&](float dt) {
                ++ticks;
                EXPECT_FLOAT_EQ(dt, 0.016f);
                if (ticks == 3) {
                    app.quit();
                }
            },
            16, 0.016f);
    EXPECT_EQ(ticks, 3);
    EXPECT_EQ(frames, 3);
    EXPECT_FALSE(app.running);
}

TEST(WebLoop, PumpHonorsMaxFrames) {
    engine::ApplicationState app;
    app.running = true;
    int ticks = 0;
    engine::MainLoopPolicy policy{engine::LoopKind::Blocking};
    const int frames = policy.pump(app, [&](float) { ++ticks; }, 4, engine::kFixed);
    EXPECT_EQ(ticks, 4);
    EXPECT_EQ(frames, 4);
    EXPECT_TRUE(app.running);
}

TEST(WebLoop, PumpZeroMaxFramesDoesNothing) {
    engine::ApplicationState app;
    app.running = true;
    int ticks = 0;
    engine::MainLoopPolicy policy;
    EXPECT_EQ(policy.pump(app, [&](float) { ++ticks; }, 0, 0.1f), 0);
    EXPECT_EQ(ticks, 0);
}

TEST(WebLoop, PumpSkipsWhenAlreadyQuit) {
    engine::ApplicationState app;
    app.running = false;
    int ticks = 0;
    engine::MainLoopPolicy policy{engine::LoopKind::RequestAnimationFrame};
    EXPECT_EQ(policy.pump(app, [&](float) { ++ticks; }, 8, 0.1f), 0);
    EXPECT_EQ(ticks, 0);
}
