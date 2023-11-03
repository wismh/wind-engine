#include <gtest/gtest.h>

#include "engine/engine.h"

TEST(Scaffold, ApiEpoch) {
    EXPECT_EQ(engine::kApiEpoch, engine::api_epoch());
}

TEST(Scaffold, DefaultTestsHaveNoWindowBackend) {
#ifndef ENGINE_WITH_WINDOW
    SUCCEED();
#else
    GTEST_SKIP() << "window backend preset; Engine::Run is still not invoked";
#endif
}
