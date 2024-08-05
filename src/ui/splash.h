#pragma once

#include <engine/igame.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine::ui {

struct SplashDocument {
    UiDocument document;
    Stylesheet stylesheet;
    // UiCanvas::reference_size for UiFit::ScaleWithScreenSize: image_size inflated so the image,
    // drawn at 80% width/height centered within it, keeps a minimum 10% margin on every edge
    // once the canvas itself is letterboxed to the real window (canvas_layout_space's existing
    // contain-fit math) - not a stretch-to-fill box.
    glm::vec2 reference_size{0.0f, 0.0f};
};

// Builds the splash's XML/CSS from `config` and `image_size` (the configured image's actual
// decoded pixel dimensions - needed to keep its aspect ratio rather than stretching it to fill
// the canvas) and runs them through the real parse_xml/parse_css (SDD §20.3) instead of
// hand-assembling Element/Keyframes structs, so a malformed generated string fails the same way
// bad game-authored markup would. nullopt when there is nothing to show: disabled, all three
// durations are non-positive (would otherwise divide by zero building the keyframe percentages),
// or `image_size` is non-positive (the image's real size wasn't resolvable, so there's no
// sensible aspect ratio to lay out against).
[[nodiscard]] std::optional<SplashDocument> build_splash_document(
        const SplashScreen& config, glm::vec2 image_size);

}
