#include <gtest/gtest.h>

#include "core/back_key_filter.h"

#include <engine/core/app_lifecycle.h>
#include <engine/core/application_state.h>
#include <engine/core/input_system.h>
#include <engine/core/key_code.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

#include <vector>

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

TEST(AndroidLifecycle, BackWithoutTextInputIsDeliveredToTheGame) {
    using Route = engine::BackKeyFilter::Route;
    engine::BackKeyFilter filter;
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, false, false), Route::Deliver);
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, true, false), Route::Deliver);
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, false, false, false), Route::Deliver);
}

TEST(AndroidLifecycle, BackBoundByTheGameSendsItsAction) {
    engine::ecs::World world;
    engine::InputSystem input;
    input.set_router([&world](engine::WindowId) { return &world; });
    const engine::ActionId back = input.intern("back");
    input.bind(engine::KeyCode::AcBack, back);

    engine::BackKeyFilter filter;
    ASSERT_EQ(filter.route(engine::KeyCode::AcBack, true, false, false), engine::BackKeyFilter::Route::Deliver);
    input.handle_key(engine::KeyCode::AcBack, true);

    std::vector<engine::InputEvent> events;
    for (const engine::InputEvent& event : engine::ecs::EventReader<engine::InputEvent>{world}) {
        events.push_back(event);
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, back);
    EXPECT_EQ(events[0].kind, engine::InputEvent::Kind::Down);
}

TEST(AndroidLifecycle, BackThatDismissesTextInputIsNotDelivered) {
    using Route = engine::BackKeyFilter::Route;
    engine::BackKeyFilter filter;
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, false, true), Route::DismissTextInput);
    // Dismissing closes the keyboard, so the repeat and release arrive with text input already off.
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, true, false), Route::Swallow);
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, false, false, false), Route::Swallow);
    // The next press, with the keyboard closed, goes to the game.
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, false, false), Route::Deliver);
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, false, false, false), Route::Deliver);
}

TEST(AndroidLifecycle, OtherKeysAreDeliveredWhileTextInputIsActive) {
    using Route = engine::BackKeyFilter::Route;
    engine::BackKeyFilter filter;
    EXPECT_EQ(filter.route(engine::KeyCode::Escape, true, false, true), Route::Deliver);
    EXPECT_EQ(filter.route(engine::KeyCode::AcBack, true, false, true), Route::DismissTextInput);
    EXPECT_EQ(filter.route(engine::KeyCode::Return, false, false, true), Route::Deliver);
}

TEST(AndroidLifecycle, PauseThenResumeKeepsRunning) {
    engine::ApplicationState app;
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::WillEnterBackground);
    engine::apply_app_lifecycle(app, engine::AppLifecycleEvent::DidEnterForeground);
    EXPECT_FALSE(app.paused);
    EXPECT_TRUE(app.running);
}
