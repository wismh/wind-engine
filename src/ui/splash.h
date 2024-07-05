#pragma once

#include <engine/igame.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <optional>

namespace engine::ui {

struct SplashDocument {
    UiDocument document;
    Stylesheet stylesheet;
};

// Builds the splash's XML/CSS from `config` and runs them through the real parse_xml/parse_css
// (SDD §20.3) instead of hand-assembling Element/Keyframes structs, so a malformed generated
// string fails the same way bad game-authored markup would. nullopt when there is nothing to
// show: disabled, or all three durations are non-positive (would otherwise divide by zero
// building the keyframe percentages).
[[nodiscard]] std::optional<SplashDocument> build_splash_document(const SplashScreen& config);

}
