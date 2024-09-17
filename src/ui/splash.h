#pragma once

#include <engine/igame.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine::ui {

struct SplashDocument {
    // Backdrop: a UiFit::FillWindow canvas holding just a solid black root, no children. Its rect
    // is always the real window rect regardless of aspect ratio, so the black backdrop covers the
    // whole window - not just the image's own letterboxed area - even when the window's aspect
    // ratio differs sharply from the image's (e.g. a tall settings window vs. a landscape splash
    // image). See show_splash for why this is a separate canvas from the image below.
    UiDocument backdrop_document;
    Stylesheet backdrop_stylesheet;
    // Image: a UiFit::ScaleWithScreenSize canvas holding the animated Image, drawn on top of the
    // backdrop (see show_splash's canvas ordering). No background of its own - the backdrop
    // canvas beneath already covers the whole window, including whatever this canvas's own
    // contain-fit rect doesn't reach.
    UiDocument image_document;
    Stylesheet image_stylesheet;
    // UiCanvas::reference_size for the image canvas's UiFit::ScaleWithScreenSize: image_size
    // inflated so the image, drawn at 80% width/height centered within it, keeps a minimum 10%
    // margin on every edge once the canvas itself is letterboxed to the real window
    // (canvas_layout_space's existing contain-fit math) - not a stretch-to-fill box.
    glm::vec2 reference_size{0.0f, 0.0f};
    // fade_in + hold + fade_out. The caller destroys both spawned entities once this much real
    // time has passed - the backdrop's black background is deliberately NOT animated (constant
    // opaque for its whole lifetime, see build_splash_document), so nothing else makes it go
    // away; without an explicit despawn it would permanently black out the game once the image's
    // fade-out finishes instead of just covering the splash itself.
    float total_duration = 0.0f;
};

// Builds the splash's two XML/CSS documents (backdrop + image, see SplashDocument) from `config`
// and `image_size` (the configured image's actual decoded pixel dimensions - needed to keep its
// aspect ratio rather than stretching it to fill the canvas) and runs them through the real
// parse_xml/parse_css (SDD §20.3) instead of hand-assembling Element/Keyframes structs, so a
// malformed generated string fails the same way bad game-authored markup would. nullopt when
// there is nothing to show: disabled, all three durations are non-positive (would otherwise
// divide by zero building the keyframe percentages), or `image_size` is non-positive (the image's
// real size wasn't resolvable, so there's no sensible aspect ratio to lay out against).
[[nodiscard]] std::optional<SplashDocument> build_splash_document(
        const SplashScreen& config, glm::vec2 image_size);

}
