#include "ui/splash.h"

#include <engine/ui/canvas.h>
#include <engine/ui/splash.h>

#include <format>
#include <string>
#include <vector>

namespace engine::ui {
namespace {

constexpr std::string_view kKeyframesName = "engine-splash-fade";
constexpr std::string_view kRootClass = "engine-splash-root";
constexpr std::string_view kImageClass = "engine-splash-image";

// Above any order a game plausibly picks for its own UI (existing usage sticks to small
// integers like 0-2), so the splash always paints last without games needing to coordinate.
constexpr int kSplashCanvasOrder = 1000;

}

std::optional<SplashDocument> build_splash_document(const SplashScreen& config, glm::vec2 image_size) {
    if (!config.enabled) {
        return std::nullopt;
    }
    const float total = config.fade_in_seconds + config.hold_seconds + config.fade_out_seconds;
    if (total <= 0.0f) {
        return std::nullopt;
    }
    if (image_size.x <= 0.0f || image_size.y <= 0.0f) {
        return std::nullopt;
    }

    const float hold_starts_pct = 100.0f * config.fade_in_seconds / total;
    const float hold_ends_pct = 100.0f * (config.fade_in_seconds + config.hold_seconds) / total;

    // Root canvas: solid black, constant (no animation-name - stays fully opaque for the whole
    // splash lifetime) so the game underneath, already running by the time this spawns, never
    // shows through during the image's own fade. The image itself is a fixed 80%-of-canvas box,
    // centered (position: absolute; left/top: 10%): preserves its own aspect ratio (no stretch)
    // and, combined with reference_size below inflating the canvas box before the real window
    // letterboxes it, keeps a minimum 10% margin on every edge rather than touching it.
    const std::string css = std::format(
            "@keyframes {0} {{\n"
            "  0% {{ opacity: 0; }}\n"
            "  {1}% {{ opacity: 1; }}\n"
            "  {2}% {{ opacity: 1; }}\n"
            "  100% {{ opacity: 0; }}\n"
            "}}\n"
            ".{3} {{ background: #000000; }}\n"
            ".{4} {{ animation-name: {0}; animation-duration: {5}s; position: absolute; "
            "left: 10%; top: 10%; width: 80%; height: 80%; }}\n",
            kKeyframesName, hold_starts_pct, hold_ends_pct, kRootClass, kImageClass, total);

    const std::string xml = std::format(
            R"(<Canvas class="{}"><Image class="{}" source="{}"/></Canvas>)", kRootClass, kImageClass,
            config.image.hex());

    auto document = parse_xml(xml);
    if (!document) {
        return std::nullopt;
    }

    std::vector<std::string> warnings;
    auto stylesheet = parse_css(css, warnings);
    if (!stylesheet) {
        return std::nullopt;
    }

    constexpr float kContentFraction = 0.8f;  // matches the 80% width/height above
    const glm::vec2 reference_size = image_size / kContentFraction;

    return SplashDocument{std::move(*document), std::move(*stylesheet), reference_size, total};
}

std::optional<ecs::Entity> show_splash(
        ecs::World& world, const SplashScreen& config, glm::vec2 image_size, WindowId window) {
    const std::optional<SplashDocument> splash = build_splash_document(config, image_size);
    if (!splash) {
        return std::nullopt;
    }
    const ecs::Entity entity = world.create();
    UiCanvas canvas;
    canvas.fit = UiFit::ScaleWithScreenSize;
    canvas.reference_size = splash->reference_size;
    canvas.order = kSplashCanvasOrder;
    canvas.window = window;
    world.emplace<UiCanvas>(entity, canvas);
    // canvas.document/data_context are left at their defaults so they match UiInstance's
    // freshly-constructed loaded_document/loaded_data_context - otherwise run_bind's
    // instance_needs_rebuild() sees a mismatch and clone_document() overwrites this in-memory
    // document by trying (and failing) to load canvas.document from the asset catalog.
    world.emplace<UiInstance>(entity, UiInstance{splash->document, splash->stylesheet});
    world.emplace<SplashTimer>(entity, SplashTimer{.total_duration = splash->total_duration});
    return entity;
}

}
