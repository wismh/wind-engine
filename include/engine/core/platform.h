#pragma once

#include <filesystem>

namespace engine {

enum class Platform { Native, Web, Android };

enum class LoopKind { Blocking, RequestAnimationFrame };

struct GraphicsProfile {
    enum class Api { OpenGl33Core, WebGl2, Gles3 };

    Api api = Api::OpenGl33Core;
    int major = 3;
    int minor = 3;
    bool es = false;

    constexpr bool operator==(const GraphicsProfile&) const noexcept = default;
};

[[nodiscard]] constexpr bool is_emscripten_build() noexcept {
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr bool is_android_build() noexcept {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr bool web_profile_enabled() noexcept {
#if defined(ENGINE_WITH_WEB) && ENGINE_WITH_WEB
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr bool android_profile_enabled() noexcept {
#if defined(ENGINE_WITH_ANDROID) && ENGINE_WITH_ANDROID
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr bool gles_profile_enabled() noexcept {
#if defined(ENGINE_WITH_GLES) && ENGINE_WITH_GLES
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr Platform current_platform() noexcept {
    if (is_emscripten_build()) {
        return Platform::Web;
    }
    if (is_android_build()) {
        return Platform::Android;
    }
    return Platform::Native;
}

[[nodiscard]] constexpr LoopKind loop_kind_for(Platform platform) noexcept {
    return platform == Platform::Web ? LoopKind::RequestAnimationFrame : LoopKind::Blocking;
}

[[nodiscard]] constexpr LoopKind default_loop_kind() noexcept {
    return loop_kind_for(current_platform());
}

[[nodiscard]] constexpr GraphicsProfile graphics_profile_for(Platform platform) noexcept {
    if (platform == Platform::Web) {
        return GraphicsProfile{
                .api = GraphicsProfile::Api::WebGl2,
                .major = 3,
                .minor = 0,
                .es = true,
        };
    }
    if (platform == Platform::Android) {
        return GraphicsProfile{
                .api = GraphicsProfile::Api::Gles3,
                .major = 3,
                .minor = 0,
                .es = true,
        };
    }
    return GraphicsProfile{
            .api = GraphicsProfile::Api::OpenGl33Core,
            .major = 3,
            .minor = 3,
            .es = false,
    };
}

[[nodiscard]] constexpr GraphicsProfile default_graphics_profile() noexcept {
    return graphics_profile_for(current_platform());
}

[[nodiscard]] constexpr bool uses_gles(Platform platform) noexcept {
    return platform == Platform::Web || platform == Platform::Android;
}

[[nodiscard]] constexpr bool audio_requires_user_gesture(Platform platform) noexcept {
    return platform == Platform::Web;
}

[[nodiscard]] constexpr bool audio_requires_user_gesture() noexcept {
    return audio_requires_user_gesture(current_platform());
}

[[nodiscard]] std::filesystem::path packaged_assets_mount() noexcept;

[[nodiscard]] std::filesystem::path apk_assets_mount() noexcept;

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform);

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path);

// Copy a cooked assets tree to dest_root so std::ifstream (AssetsDb) can read it.
bool stage_android_assets(const std::filesystem::path& src_root, const std::filesystem::path& dest_root);

}
