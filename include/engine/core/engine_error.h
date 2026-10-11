#pragma once

// docs/tech/modules/Core.md

#include <string_view>

namespace engine {

// Why the windowed engine could not start. `EngineHost::init`, `EngineHost::open_primary`, and `Engine<GameT>::init`
// return it. None of them shows a dialog: the entry point does, once, with `message`.
enum class EngineError {
    VideoInit,
    WindowCreate,
    AudioInit,
    HapticsInit,
    AssetsRoot,
    EngineCatalog,
    UiFont,
    GameCatalog,
};

// The enumerator name, for logs and tests.
[[nodiscard]] constexpr std::string_view to_string(EngineError error) noexcept {
    switch (error) {
        case EngineError::VideoInit:
            return "VideoInit";
        case EngineError::WindowCreate:
            return "WindowCreate";
        case EngineError::AudioInit:
            return "AudioInit";
        case EngineError::HapticsInit:
            return "HapticsInit";
        case EngineError::AssetsRoot:
            return "AssetsRoot";
        case EngineError::EngineCatalog:
            return "EngineCatalog";
        case EngineError::UiFont:
            return "UiFont";
        case EngineError::GameCatalog:
            return "GameCatalog";
    }
    return "Unknown";
}

// The sentence shown to the user in the fatal dialog.
[[nodiscard]] constexpr std::string_view message(EngineError error) noexcept {
    switch (error) {
        case EngineError::VideoInit:
            return "Failed to initialize the video system";
        case EngineError::WindowCreate:
            return "Failed to create the game window";
        case EngineError::AudioInit:
            return "Failed to initialize audio";
        case EngineError::HapticsInit:
            return "Failed to initialize haptics";
        case EngineError::AssetsRoot:
            return "Assets root is missing";
        case EngineError::EngineCatalog:
            return "Failed to load engine catalog";
        case EngineError::UiFont:
            return "Failed to load UI font";
        case EngineError::GameCatalog:
            return "Failed to load game catalog";
    }
    return "Unknown startup error";
}

}
