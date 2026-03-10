#pragma once

// docs/tech/modules/Core.md

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

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

// Whether IHaptics::vibrate()'s intensity is honored as real amplitude on this platform,
// as opposed to being accepted but only usable as an on/off gate. This is a compile-time
// simplification: even on Android, amplitude control additionally requires API 26+ at
// runtime, which cannot be represented as a pure Platform predicate.
[[nodiscard]] constexpr bool haptics_has_amplitude_control(Platform platform) noexcept {
    return platform == Platform::Android;
}

[[nodiscard]] std::filesystem::path packaged_assets_mount() noexcept;

// SDL Android I/O prefix. Returned as a string because std::filesystem::path
// treats "assets:" as a drive letter and would collapse "assets://" to "assets:/".
[[nodiscard]] std::string apk_assets_mount() noexcept;

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform);

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path);

// Copy a cooked assets tree to dest_root so std::ifstream (AssetsDb) can read it.
bool stage_android_assets(const std::filesystem::path& src_root, const std::filesystem::path& dest_root);

// docs/tech/build/Runtime Assets.md
// Native and web: default_assets_root(base). Android: stage the cooked catalog onto internal storage
// so AssetsDb can ifstream it. Empty base still goes through default_assets_root (web maps that to /assets).
[[nodiscard]] std::filesystem::path runtime_assets_root(const std::filesystem::path& base_path);

// SDL's Android pref path is the internal-storage root, which also holds staged assets/.
// Game writes go under user/ so the two trees stay disjoint.
[[nodiscard]] inline std::filesystem::path android_user_data_directory(
        const std::filesystem::path& internal_storage) {
    return internal_storage / "user";
}

// Writable per-user directory for this organization and application. Both names are one path
// segment: non-empty valid UTF-8, no leading or trailing ASCII space, no trailing dot, none of
// / \ : * ? " < > | or ASCII controls, and not a Windows device name (CON, PRN, AUX, NUL,
// COM1-COM9, LPT1-LPT9, including a dotted suffix such as CON.txt). Those strings are the
// directory identity; changing them leaves old saves behind.
//
// Desktop: SDL pref path — %APPDATA% on Windows, ~/.local/share or $XDG_DATA_HOME on Unix,
// ~/Library/Application Support on macOS. Web: /storage/<org>/<app>/ via IndexedDB
// (engine_add_sdl3 sets SDL_EMSCRIPTEN_PERSISTENT_PATH). The browser may flush that write a few
// frames later. Android: <internal storage>/user/, beside staged assets/. SDL's Android pref path
// is that internal-storage root and ignores these two strings; applicationId isolates apps.
//
// errc::invalid_argument — bad name. errc::function_not_supported — no SDL (ENGINE_WITH_WINDOW
// off). errc::io_error — SDL could not create the pref directory. On Android, user/ is created
// afterwards and a failure there returns that filesystem error_code. Main thread only; on Android,
// call after Engine::init.
[[nodiscard]] std::expected<std::filesystem::path, std::error_code> user_data_directory(
        std::string_view organization, std::string_view application);

}
