#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

TEST(Scaffold, DefaultTestsHaveNoAndroidProfile) {
#ifndef ENGINE_WITH_ANDROID
    SUCCEED();
#else
    GTEST_SKIP() << "android profile preset; Engine::run is still not invoked";
#endif
}

TEST(Scaffold, DefaultTestsHaveNoGlesProfile) {
#ifndef ENGINE_WITH_GLES
    SUCCEED();
#else
    GTEST_SKIP() << "gles profile preset; Engine::run is still not invoked";
#endif
}

TEST(Scaffold, WebCmakeFilesExist) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "web" / "shell.html"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "web" / "link_flags.cmake"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "toolchains" / "Emscripten.cmake"));
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidCmakeFilesExist) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "toolchains" / "android-ndk.cmake"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "android" / "settings.gradle"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "android" / "build.gradle"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "cmake" / "android" / "app" / "build.gradle"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
            root / "cmake" / "android" / "app" / "src" / "main" / "AndroidManifest.xml"));
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidManifestDeclaresVibratePermission) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string manifest =
            slurp(root / "cmake" / "android" / "app" / "src" / "main" / "AndroidManifest.xml");
    ASSERT_FALSE(manifest.empty());
    EXPECT_NE(manifest.find("android.permission.VIBRATE"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidCmakeAndGradleShareAssetsOut) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string cmake = slurp(root / "CMakeLists.txt");
    const std::string gradle = slurp(root / "cmake" / "android" / "app" / "build.gradle");
    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(gradle.empty());
    EXPECT_NE(cmake.find("ENGINE_ANDROID_ASSETS_OUT"), std::string::npos);
    EXPECT_NE(gradle.find("ENGINE_ANDROID_ASSETS_OUT"), std::string::npos);
    EXPECT_NE(gradle.find("-DENGINE_ANDROID_ASSETS_OUT="), std::string::npos);
    EXPECT_NE(gradle.find("wind-assets"), std::string::npos);
    EXPECT_EQ(gradle.find("build-android/android-assets"), std::string::npos);
    EXPECT_EQ(gradle.find("hasProperty('ENGINE_ANDROID_ASSETS')"), std::string::npos);
    EXPECT_EQ(cmake.find("CMAKE_BINARY_DIR}/android-assets"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, EngineAddGameGeneratesIconsOnce) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string cmake = slurp(root / "CMakeLists.txt");
    ASSERT_FALSE(cmake.empty());
    // One shared add_custom_command per game target — per-platform packaging tasks must depend on
    // ${target}_icons / ENGINE_GAME_ICON_DIR instead of invoking icon_codegen themselves, or two
    // independent custom commands would race to write the same generated files.
    EXPECT_NE(cmake.find("ENGINE_GAME_ICON_DIR"), std::string::npos);
    EXPECT_NE(cmake.find("${target}_icons"), std::string::npos);
    EXPECT_NE(cmake.find("COMMAND icon_codegen"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidManifestDeclaresLauncherIcon) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string manifest =
            slurp(root / "cmake" / "android" / "app" / "src" / "main" / "AndroidManifest.xml");
    ASSERT_FALSE(manifest.empty());
    EXPECT_NE(manifest.find(R"(android:icon="@mipmap/ic_launcher")"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidGradleOverlaysPerGameResDirAndApplicationId) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string gradle = slurp(root / "cmake" / "android" / "app" / "build.gradle");
    ASSERT_FALSE(gradle.empty());
    // A game supplies its own mipmap-*/ic_launcher.png (and optionally values/strings.xml) via
    // ENGINE_ANDROID_RES_DIR; AGP's standard resource merger overlays it over the engine's own
    // res/ by name, without ever editing the committed engine template (SDD §19.4).
    EXPECT_NE(gradle.find("ENGINE_ANDROID_RES_DIR"), std::string::npos);
    EXPECT_NE(gradle.find("res.srcDirs"), std::string::npos);
    // Two games must not collide under the same applicationId; default stays the engine's own.
    EXPECT_NE(gradle.find("ENGINE_ANDROID_APPLICATION_ID"), std::string::npos);
    EXPECT_NE(gradle.find("applicationId gameApplicationId"), std::string::npos);
    EXPECT_NE(gradle.find("'org.windengine.app'"), std::string::npos);
    // The overlay must never require editing the engine's committed manifest/strings/res per game.
    EXPECT_EQ(gradle.find("applicationId 'org.windengine.app'"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}
