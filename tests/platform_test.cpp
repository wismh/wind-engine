#include <gtest/gtest.h>

#include <engine/core/platform.h>
#include <engine/engine.h>

#include <filesystem>

TEST(Platform, NativeDefaultsOnThisBuild) {
    EXPECT_EQ(engine::current_platform(), engine::Platform::Native);
    EXPECT_FALSE(engine::is_emscripten_build());
    EXPECT_FALSE(engine::web_profile_enabled());
    EXPECT_FALSE(engine::android_profile_enabled());
    EXPECT_FALSE(engine::gles_profile_enabled());
    EXPECT_EQ(engine::default_loop_kind(), engine::LoopKind::Blocking);
    EXPECT_EQ(engine::default_graphics_profile().api, engine::GraphicsProfile::Api::OpenGl33Core);
    EXPECT_EQ(engine::default_graphics_profile().major, 3);
    EXPECT_EQ(engine::default_graphics_profile().minor, 3);
    EXPECT_FALSE(engine::default_graphics_profile().es);
    EXPECT_FALSE(engine::audio_requires_user_gesture());
}

TEST(Platform, WebProfileConstants) {
    EXPECT_EQ(engine::loop_kind_for(engine::Platform::Web), engine::LoopKind::RequestAnimationFrame);
    EXPECT_EQ(engine::loop_kind_for(engine::Platform::Native), engine::LoopKind::Blocking);

    const engine::GraphicsProfile web = engine::graphics_profile_for(engine::Platform::Web);
    EXPECT_EQ(web.api, engine::GraphicsProfile::Api::WebGl2);
    EXPECT_EQ(web.major, 3);
    EXPECT_EQ(web.minor, 0);
    EXPECT_TRUE(web.es);

    EXPECT_TRUE(engine::audio_requires_user_gesture(engine::Platform::Web));
    EXPECT_FALSE(engine::audio_requires_user_gesture(engine::Platform::Native));
}

TEST(Platform, PackagedAssetsMount) {
    EXPECT_EQ(engine::packaged_assets_mount(), std::filesystem::path{"/assets"});
}

TEST(Platform, NativeAssetsRootUnderBase) {
    const std::filesystem::path root = engine::default_assets_root(std::filesystem::path{"/tmp/game"},
            engine::Platform::Native);
    EXPECT_EQ(root, std::filesystem::path{"/tmp/game"} / "assets");
}

TEST(Platform, NativeEmptyBaseYieldsEmptyRoot) {
    EXPECT_TRUE(engine::default_assets_root({}, engine::Platform::Native).empty());
}

TEST(Platform, WebAssetsRootIsPackagedMount) {
    EXPECT_EQ(engine::default_assets_root(std::filesystem::path{"/tmp/game"}, engine::Platform::Web),
            engine::packaged_assets_mount());
    EXPECT_EQ(engine::default_assets_root({}, engine::Platform::Web), engine::packaged_assets_mount());
}

TEST(Platform, EngineRuntimeEmptyBaseFollowsHelperNotShortCircuit) {
    // EngineRuntime::assets_root() must call default_assets_root(base) even when
    // SDL_GetBasePath() is empty. A native `if (base.empty()) return {}` would
    // fatal Engine::init on Emscripten ("Assets root is missing").
    const std::filesystem::path empty_sdl_base{};
    EXPECT_EQ(engine::default_assets_root(empty_sdl_base, engine::Platform::Web),
            engine::packaged_assets_mount());
    EXPECT_FALSE(engine::default_assets_root(empty_sdl_base, engine::Platform::Web).empty());
    EXPECT_TRUE(engine::default_assets_root(empty_sdl_base, engine::Platform::Native).empty());
    EXPECT_TRUE(engine::default_assets_root(empty_sdl_base, engine::Platform::Android).empty());
    EXPECT_EQ(engine::default_assets_root(empty_sdl_base),
            engine::default_assets_root(empty_sdl_base, engine::current_platform()));
}

TEST(Platform, OneArgAssetsRootMatchesCurrentPlatform) {
    EXPECT_EQ(engine::default_assets_root(std::filesystem::path{"/opt/app"}),
            engine::default_assets_root(std::filesystem::path{"/opt/app"}, engine::current_platform()));
}

TEST(Platform, ApiEpochIsFour) {
    EXPECT_EQ(engine::kApiEpoch, 4);
    EXPECT_EQ(engine::api_epoch(), 4);
}

TEST(Platform, AndroidProfileConstants) {
    EXPECT_EQ(engine::loop_kind_for(engine::Platform::Android), engine::LoopKind::Blocking);
    EXPECT_TRUE(engine::uses_gles(engine::Platform::Android));
    EXPECT_TRUE(engine::uses_gles(engine::Platform::Web));
    EXPECT_FALSE(engine::uses_gles(engine::Platform::Native));

    const engine::GraphicsProfile android = engine::graphics_profile_for(engine::Platform::Android);
    EXPECT_EQ(android.api, engine::GraphicsProfile::Api::Gles3);
    EXPECT_EQ(android.major, 3);
    EXPECT_EQ(android.minor, 0);
    EXPECT_TRUE(android.es);

    EXPECT_FALSE(engine::audio_requires_user_gesture(engine::Platform::Android));
    EXPECT_FALSE(engine::is_android_build());
    EXPECT_FALSE(engine::android_profile_enabled());
    EXPECT_FALSE(engine::gles_profile_enabled());
}

TEST(Platform, HapticsAmplitudeControl) {
    EXPECT_TRUE(engine::haptics_has_amplitude_control(engine::Platform::Android));
    EXPECT_FALSE(engine::haptics_has_amplitude_control(engine::Platform::Web));
    EXPECT_FALSE(engine::haptics_has_amplitude_control(engine::Platform::Native));
}

TEST(Platform, AndroidAssetsRoot) {
    EXPECT_EQ(engine::apk_assets_mount(), "assets://");
    EXPECT_TRUE(engine::default_assets_root({}, engine::Platform::Android).empty());
    EXPECT_EQ(engine::default_assets_root(std::filesystem::path{"/data/app"}, engine::Platform::Android),
            std::filesystem::path{"/data/app"} / "assets");
}
