#pragma once

#include <filesystem>

namespace engine {

enum class Platform { Native, Web };

enum class LoopKind { Blocking, RequestAnimationFrame };

struct GraphicsProfile {
    enum class Api { OpenGl33Core, WebGl2 };

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

[[nodiscard]] constexpr bool web_profile_enabled() noexcept {
#if defined(ENGINE_WITH_WEB) && ENGINE_WITH_WEB
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr Platform current_platform() noexcept {
    return is_emscripten_build() ? Platform::Web : Platform::Native;
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

[[nodiscard]] constexpr bool audio_requires_user_gesture(Platform platform) noexcept {
    return platform == Platform::Web;
}

[[nodiscard]] constexpr bool audio_requires_user_gesture() noexcept {
    return audio_requires_user_gesture(current_platform());
}

[[nodiscard]] std::filesystem::path packaged_assets_mount() noexcept;

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform);

[[nodiscard]] std::filesystem::path default_assets_root(const std::filesystem::path& base_path);

}
