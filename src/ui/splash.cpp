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

    // Backdrop: solid black, constant (no animation-name - stays fully opaque for the whole
    // splash lifetime) so the game underneath, already running by the time this spawns, never
    // shows through during the image's own fade. It has no children and is spawned on its own
    // UiFit::FillWindow canvas (show_splash) so it always covers the entire window - a
    // UiFit::ScaleWithScreenSize canvas letterboxes to the image's own aspect ratio, which would
    // leave the backdrop itself letterboxed (and the game's UI visible through the gap) whenever
    // the window's aspect ratio doesn't match the image's.
    const std::string backdrop_css = std::format(".{} {{ background: #000000; }}\n", kRootClass);
    const std::string backdrop_xml = std::format(R"(<Canvas class="{}"/>)", kRootClass);

    // Image: a fixed 80%-of-canvas box, centered (position: absolute; left/top: 10%): preserves
    // its own aspect ratio (no stretch) and, combined with reference_size below inflating the
    // canvas box before the real window letterboxes it, keeps a minimum 10% margin on every edge
    // rather than touching it. Spawned on its own UiFit::ScaleWithScreenSize canvas, drawn above
    // the backdrop (show_splash) - the backdrop already covers the window, so this canvas needs
    // no background of its own.
    const std::string image_css = std::format(
            "@keyframes {0} {{\n"
            "  0% {{ opacity: 0; }}\n"
            "  {1}% {{ opacity: 1; }}\n"
            "  {2}% {{ opacity: 1; }}\n"
            "  100% {{ opacity: 0; }}\n"
            "}}\n"
            ".{3} {{ animation-name: {0}; animation-duration: {4}s; position: absolute; "
            "left: 10%; top: 10%; width: 80%; height: 80%; }}\n",
            kKeyframesName, hold_starts_pct, hold_ends_pct, kImageClass, total);

    const std::string image_xml =
            std::format(R"(<Canvas class="{}"><Image class="{}" source="{}"/></Canvas>)", kRootClass, kImageClass,
                    config.image.hex());

    auto backdrop_document = parse_xml(backdrop_xml);
    auto image_document = parse_xml(image_xml);
    if (!backdrop_document || !image_document) {
        return std::nullopt;
    }

    std::vector<std::string> warnings;
    auto backdrop_stylesheet = parse_css(backdrop_css, warnings);
    auto image_stylesheet = parse_css(image_css, warnings);
    if (!backdrop_stylesheet || !image_stylesheet) {
        return std::nullopt;
    }

    constexpr float kContentFraction = 0.8f;  // matches the 80% width/height above
    const glm::vec2 reference_size = image_size / kContentFraction;

    return SplashDocument{
            std::move(*backdrop_document),
            std::move(*backdrop_stylesheet),
            std::move(*image_document),
            std::move(*image_stylesheet),
            reference_size,
            total,
    };
}

std::optional<ecs::Entity> show_splash(
        ecs::World& world, const SplashScreen& config, glm::vec2 image_size, WindowId window) {
    const std::optional<SplashDocument> splash = build_splash_document(config, image_size);
    if (!splash) {
        return std::nullopt;
    }

    // Two canvases, not one: a UiFit::FillWindow backdrop that always covers the whole window,
    // and a UiFit::ScaleWithScreenSize image layer on top of it that letterboxes to the image's
    // own aspect ratio. Splitting them is what keeps the backdrop opaque everywhere regardless of
    // the window's aspect ratio - see build_splash_document. kSplashCanvasOrder/+1 keeps both
    // above any order a game plausibly picks for its own UI while ordering the image above the
    // backdrop.
    const ecs::Entity backdrop = world.create();
    UiCanvas backdrop_canvas;
    backdrop_canvas.fit = UiFit::FillWindow;
    backdrop_canvas.order = kSplashCanvasOrder;
    backdrop_canvas.window = window;
    world.emplace<UiCanvas>(backdrop, backdrop_canvas);
    // canvas.document/data_context are left at their defaults so they match UiInstance's
    // freshly-constructed loaded_document/loaded_data_context - otherwise run_bind's
    // instance_needs_rebuild() sees a mismatch and clone_document() overwrites this in-memory
    // document by trying (and failing) to load canvas.document from the asset catalog.
    world.emplace<UiInstance>(backdrop, UiInstance{splash->backdrop_document, splash->backdrop_stylesheet});
    world.emplace<SplashTimer>(backdrop, SplashTimer{.total_duration = splash->total_duration});

    const ecs::Entity image = world.create();
    UiCanvas image_canvas;
    image_canvas.fit = UiFit::ScaleWithScreenSize;
    image_canvas.reference_size = splash->reference_size;
    image_canvas.order = kSplashCanvasOrder + 1;
    image_canvas.window = window;
    world.emplace<UiCanvas>(image, image_canvas);
    world.emplace<UiInstance>(image, UiInstance{splash->image_document, splash->image_stylesheet});
    // Same total_duration, started this same frame: run_splash_timers ages both by identical
    // per-frame delta_time, so they always cross total_duration and despawn on the same frame -
    // no explicit link between the two entities is needed to keep them in sync.
    world.emplace<SplashTimer>(image, SplashTimer{.total_duration = splash->total_duration});

    return backdrop;
}

}
