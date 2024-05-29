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

TEST(Scaffold, WebShellHtmlReferencesFavicon) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string shell = slurp(root / "cmake" / "web" / "shell.html");
    ASSERT_FALSE(shell.empty());
    EXPECT_NE(shell.find("<link rel=\"icon\""), std::string::npos);
    EXPECT_NE(shell.find("favicon.png"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, EngineAddGameCopiesWebFavicon) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string cmake = slurp(root / "CMakeLists.txt");
    ASSERT_FALSE(cmake.empty());

    // The favicon copy must consume the shared ENGINE_GAME_ICON_DIR output (§19.2), not
    // re-invoke icon_codegen, and must live inside engine_add_game's EMSCRIPTEN block rather
    // than e.g. the ANDROID block, so it stays a no-op on every other platform.
    const std::size_t emscripten_block = cmake.find("if(EMSCRIPTEN)\n        include(\"${ENGINE_CMAKE_DIR}/cmake/web/link_flags.cmake\")");
    ASSERT_NE(emscripten_block, std::string::npos);
    const std::size_t emscripten_endif = cmake.find("\n    endif()", emscripten_block);
    ASSERT_NE(emscripten_endif, std::string::npos);

    const std::size_t favicon_copy = cmake.find("favicon.png", emscripten_block);
    ASSERT_NE(favicon_copy, std::string::npos);
    EXPECT_LT(favicon_copy, emscripten_endif);

    const std::size_t android_block = cmake.find("if(ANDROID AND TARGET SDL3::SDL3main)");
    if (android_block != std::string::npos) {
        EXPECT_LT(favicon_copy, android_block);
    }

    EXPECT_NE(cmake.find("ENGINE_GAME_ICON_DIR", emscripten_block), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}
