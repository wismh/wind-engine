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

TEST(Scaffold, EngineAddGameBundlesMacIcon) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string cmake = slurp(root / "CMakeLists.txt");
    ASSERT_FALSE(cmake.empty());
    // The macOS block must consume the shared ENGINE_GAME_ICON_DIR / icon.icns output rather than
    // re-invoking icon_codegen (SDD §19.2) — a second add_custom_command would race the shared one.
    const auto apple_block = cmake.find("if(APPLE AND _icon_dir)");
    ASSERT_NE(apple_block, std::string::npos);
    const auto apple_block_end = cmake.find("endif()", apple_block);
    ASSERT_NE(apple_block_end, std::string::npos);
    const std::string apple_block_text = cmake.substr(apple_block, apple_block_end - apple_block);
    EXPECT_NE(apple_block_text.find("MACOSX_BUNDLE_ICON_FILE"), std::string::npos);
    EXPECT_NE(apple_block_text.find("MACOSX_PACKAGE_LOCATION"), std::string::npos);
    EXPECT_NE(apple_block_text.find("icon.icns"), std::string::npos);
    EXPECT_NE(apple_block_text.find("ENGINE_GAME_ICON_DIR"), std::string::npos);
    EXPECT_EQ(cmake.find("COMMAND icon_codegen", apple_block), std::string::npos);
    // Bundle-executable toggle must be gated on APPLE, distinct from the icon-resource block above,
    // and must precede it (target must become a bundle before its icon resource is attached).
    const auto bundle_toggle = cmake.find("PROPERTIES MACOSX_BUNDLE ON");
    ASSERT_NE(bundle_toggle, std::string::npos);
    ASSERT_LT(bundle_toggle, apple_block);
    const auto guard_start = cmake.rfind("if(APPLE)", bundle_toggle);
    ASSERT_NE(guard_start, std::string::npos);
    const auto guard_end = cmake.find("endif()", guard_start);
    ASSERT_NE(guard_end, std::string::npos);
    EXPECT_LT(bundle_toggle, guard_end) << "MACOSX_BUNDLE ON is not inside the if(APPLE) guard";
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

TEST(Scaffold, WebShellHtmlCanvasShrinksToFitNarrowViewports) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string shell = slurp(root / "cmake" / "web" / "shell.html");
    ASSERT_FALSE(shell.empty());
    // IGame::window_size() becomes the canvas's real-px drawing-buffer width/height attributes;
    // without a CSS cap, a viewport narrower than that (any phone) overflows horizontally
    // instead of the canvas shrinking to fit, preserving its aspect ratio (same replaced-element
    // behavior as an <img>).
    EXPECT_NE(shell.find("width=device-width"), std::string::npos);
    const auto canvas_rule = shell.find("#canvas");
    ASSERT_NE(canvas_rule, std::string::npos);
    const auto canvas_rule_end = shell.find("}", canvas_rule);
    ASSERT_NE(canvas_rule_end, std::string::npos);
    const std::string canvas_rule_text = shell.substr(canvas_rule, canvas_rule_end - canvas_rule);
    EXPECT_NE(canvas_rule_text.find("max-width: 100%"), std::string::npos);
    EXPECT_NE(canvas_rule_text.find("height: auto"), std::string::npos);
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
    // A game supplies its own mipmap-*/ic_launcher.png via ENGINE_ANDROID_RES_DIR. AGP only gives
    // override precedence to build-variant source sets (debug/release) over main, never to
    // sibling directories added to main's own res.srcDirs — those merge as equal siblings and
    // AAPT2 hard-fails as "Duplicate resources" the moment both declare the same resource. Since
    // wind-62 shipped a default mipmap/ic_launcher in the engine's own main res/, the overlay
    // MUST live under debug/release, not main, or every game supplying its own icon would hit
    // that same collision (app_name hit exactly this before it moved to a manifestPlaceholder —
    // see AndroidGradleSetsAppNameManifestPlaceholder below).
    EXPECT_NE(gradle.find("ENGINE_ANDROID_RES_DIR"), std::string::npos);
    const auto main_block = gradle.find("main {");
    ASSERT_NE(main_block, std::string::npos);
    const auto main_block_end = gradle.find("}", main_block);
    ASSERT_NE(main_block_end, std::string::npos);
    const auto res_in_main = gradle.find("res.srcDirs", main_block);
    EXPECT_TRUE(res_in_main == std::string::npos || res_in_main > main_block_end)
            << "res.srcDirs must not be added inside sourceSets.main";
    const auto debug_res = gradle.find("debug {");
    ASSERT_NE(debug_res, std::string::npos);
    ASSERT_GT(debug_res, main_block_end);
    EXPECT_NE(gradle.find("res.srcDirs", debug_res), std::string::npos);
    const auto release_res = gradle.find("release {", debug_res);
    ASSERT_NE(release_res, std::string::npos);
    EXPECT_NE(gradle.find("res.srcDirs", release_res), std::string::npos);
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

TEST(Scaffold, AndroidGradleThreadsHostIconCodegen) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string gradle = slurp(root / "cmake" / "android" / "app" / "build.gradle");
    ASSERT_FALSE(gradle.empty());
    // Cross-compiling for Android needs a native icon_codegen (SDD §19.3) the same way it needs a
    // native asset_codegen — mirror the existing ENGINE_HOST_ASSET_CODEGEN passthrough exactly.
    EXPECT_NE(gradle.find("ENGINE_HOST_ICON_CODEGEN"), std::string::npos);
    EXPECT_NE(gradle.find("-DENGINE_HOST_ICON_CODEGEN="), std::string::npos);
    EXPECT_NE(gradle.find("hostIconCodegen"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidGradleSetsAppNameManifestPlaceholder) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string gradle = slurp(root / "cmake" / "android" / "app" / "build.gradle");
    const std::string manifest =
            slurp(root / "cmake" / "android" / "app" / "src" / "main" / "AndroidManifest.xml");
    ASSERT_FALSE(gradle.empty());
    ASSERT_FALSE(manifest.empty());
    // app_name is a manifestPlaceholder, not a values/strings.xml resource overlay (SDD §19.4) —
    // a second res.srcDirs entry declaring string/app_name would hard-fail AAPT2 as a duplicate.
    EXPECT_NE(gradle.find("ENGINE_ANDROID_APP_NAME"), std::string::npos);
    EXPECT_NE(gradle.find("manifestPlaceholders"), std::string::npos);
    EXPECT_NE(gradle.find("appName:"), std::string::npos);
    EXPECT_NE(manifest.find(R"(android:label="${appName}")"), std::string::npos);
    EXPECT_EQ(manifest.find(R"(android:label="@string/app_name")"), std::string::npos);
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}

TEST(Scaffold, AndroidResIncludesDefaultLauncherIcon) {
#ifdef ENGINE_SOURCE_DIR
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    const std::filesystem::path res = root / "cmake" / "android" / "app" / "src" / "main" / "res";
    // android:icon="@mipmap/ic_launcher" is unconditional in the manifest and has no
    // manifestPlaceholder equivalent, so an unresolved reference is a hard AAPT2 link error, not
    // a graceful fallback — the engine must ship its own default set (SDD §19.4) so a game with no
    // icon.png/ENGINE_ANDROID_RES_DIR overlay still builds.
    for (const char* density : {"mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi", "mipmap-xxhdpi", "mipmap-xxxhdpi"}) {
        EXPECT_TRUE(std::filesystem::is_regular_file(res / density / "ic_launcher.png")) << density;
    }
#else
    GTEST_SKIP() << "ENGINE_SOURCE_DIR is not defined";
#endif
}
