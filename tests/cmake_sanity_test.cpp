#include <gtest/gtest.h>

#include <filesystem>

#include "engine/engine.h"

TEST(Scaffold, ApiEpoch) {
    EXPECT_EQ(engine::kApiEpoch, engine::api_epoch());
}

TEST(Scaffold, DefaultTestsHaveNoWindowBackend) {
#ifndef ENGINE_WITH_WINDOW
    SUCCEED();
#else
    GTEST_SKIP() << "window backend preset; Engine::run is still not invoked";
#endif
}

TEST(Scaffold, DefaultTestsHaveNoAudioBackend) {
#ifndef ENGINE_WITH_AUDIO
    SUCCEED();
#else
    GTEST_SKIP() << "audio backend preset; mixer device is still not opened in engine_tests";
#endif
}

TEST(Scaffold, DefaultTestsHaveNoWebProfile) {
#ifndef ENGINE_WITH_WEB
    SUCCEED();
#else
    GTEST_SKIP() << "web profile preset; Engine::run is still not invoked";
#endif
}

TEST(Scaffold, WebCmakeFilesExist) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "web" / "shell.html"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "web" / "link_flags.cmake"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "toolchains" / "Emscripten.cmake"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "docs" / "sdd-web.md"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "docs" / "plan-web.md"));
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}
