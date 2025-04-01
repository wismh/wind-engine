#include <gtest/gtest.h>

#include <engine/core/app_lifecycle.h>
#include <engine/core/application_state.h>

TEST(AndroidLifecycle, BackgroundPausesWithoutQuitting) {
    engine::ApplicationState app;
    app.running = true;
    app.paused = false;
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::WillEnterBackground);
    EXPECT_TRUE(app.paused);
    EXPECT_TRUE(app.running);
}

TEST(AndroidLifecycle, ForegroundUnpauses) {
    engine::ApplicationState app;
    app.running = true;
    app.paused = true;
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::DidEnterForeground);
    EXPECT_FALSE(app.paused);
    EXPECT_TRUE(app.running);
}

TEST(AndroidLifecycle, TerminatingQuits) {
    engine::ApplicationState app;
    app.running = true;
    app.paused = true;
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::Terminating);
    EXPECT_FALSE(app.running);
}

TEST(AndroidLifecycle, BackQuitsInV1) {
    EXPECT_TRUE(engine::android_back_quits());
    engine::ApplicationState app;
    app.running = true;
    engine::apply_android_back(app, false);
    EXPECT_FALSE(app.running);
}

TEST(AndroidLifecycle, BackWithActiveTextInputDoesNotQuit) {
    engine::ApplicationState app;
    app.running = true;
    engine::apply_android_back(app, true);
    EXPECT_TRUE(app.running);
    EXPECT_FALSE(app.paused);
}

TEST(AndroidLifecycle, PauseThenResumeKeepsRunning) {
    engine::ApplicationState app;
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::WillEnterBackground);
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::DidEnterForeground);
    EXPECT_FALSE(app.paused);
    EXPECT_TRUE(app.running);
}
