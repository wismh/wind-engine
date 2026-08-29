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

TEST(WebLoop, ShutdownRunsQuitThenDispose) {
    engine::LoopShutdown shutdown;
    int order = 0;
    int quit_at = 0;
    int dispose_at = 0;

    shutdown.complete([&] { quit_at = ++order; }, [&] { dispose_at = ++order; });

    EXPECT_TRUE(shutdown.quit_completed());
    EXPECT_TRUE(shutdown.dispose_completed());
    EXPECT_EQ(quit_at, 1);
    EXPECT_EQ(dispose_at, 2);
}

TEST(WebLoop, ShutdownIsIdempotent) {
    engine::LoopShutdown shutdown;
    int quits = 0;
    int disposes = 0;
    shutdown.complete([&] { ++quits; }, [&] { ++disposes; });
    shutdown.complete([&] { ++quits; }, [&] { ++disposes; });
    EXPECT_EQ(quits, 1);
    EXPECT_EQ(disposes, 1);
}

TEST(WebLoop, RafQuitInvokesDisposeAfterPump) {
    engine::ApplicationState app;
    app.running = true;
    engine::MainLoopPolicy policy{engine::LoopKind::RequestAnimationFrame};
    engine::LoopShutdown shutdown;
    int ticks = 0;
    int quits = 0;
    int disposes = 0;

    policy.pump(
            app,
            [&](float) {
                ++ticks;
                if (ticks == 2) {
                    app.quit();
                }
            },
            8, 0.016f);

    ASSERT_FALSE(app.running);
    EXPECT_FALSE(shutdown.dispose_completed());
    shutdown.complete([&] { ++quits; }, [&] { ++disposes; });
    EXPECT_EQ(ticks, 2);
    EXPECT_EQ(quits, 1);
    EXPECT_EQ(disposes, 1);
    EXPECT_TRUE(shutdown.quit_completed());
    EXPECT_TRUE(shutdown.dispose_completed());
}
