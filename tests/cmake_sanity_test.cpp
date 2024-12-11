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

TEST(Scaffold, EngineAddGameEmbedsWindowsIconResource) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string cmake = slurp(root / "CMakeLists.txt");
    ASSERT_FALSE(cmake.empty());

    // The WIN32 icon.rc block must read ENGINE_GAME_ICON_DIR (the property set by the shared
    // icon_codegen block) rather than re-deriving the generated path or re-invoking icon_codegen.
    const auto win32_icon = cmake.find("WIN32 AND _icon_dir");
    ASSERT_NE(win32_icon, std::string::npos);
    const auto icon_rc = cmake.find("icon.rc", win32_icon);
    ASSERT_NE(icon_rc, std::string::npos);
    // ENGINE_GAME_ICON_DIR is read via get_property just above the WIN32 guard, so look
    // shortly before it rather than after.
    const auto block_start = win32_icon > 200 ? win32_icon - 200 : 0;
    const auto icon_dir_ref = cmake.find("ENGINE_GAME_ICON_DIR", block_start);
    ASSERT_NE(icon_dir_ref, std::string::npos);
    EXPECT_LT(icon_dir_ref, icon_rc);  // same block, not some unrelated later mention

    // Must not leak into the platform blocks owned by the other icon rows (§19.2): the Windows
    // block precedes engine_prepare_runtime, and the Emscripten/Android wiring for engine_add_game
    // follows it, so icon.rc must land strictly before engine_target_web_preload's call site.
    const auto web_preload = cmake.find("engine_target_web_preload(${target})");
    ASSERT_NE(web_preload, std::string::npos);
    EXPECT_LT(icon_rc, web_preload);
    const auto android_res_dir = cmake.find("ENGINE_ANDROID_RES_DIR", icon_rc);
    if (android_res_dir != std::string::npos) {
        EXPECT_LT(icon_rc, android_res_dir);
    }
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}
