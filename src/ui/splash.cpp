#include "ui/splash.h"

#include <format>
#include <string>
#include <vector>

namespace engine::ui {
namespace {

constexpr std::string_view kKeyframesName = "engine-splash-fade";
constexpr std::string_view kImageClass = "engine-splash-image";

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

    // Fixed 80%-of-canvas box, centered (position: absolute; left/top: 10%): preserves the
    // image's own aspect ratio (no stretch) and, combined with reference_size below inflating
    // that canvas box before the real window letterboxes it, keeps a minimum 10% margin on
    // every edge rather than touching it.
    const std::string css = std::format(
            "@keyframes {0} {{\n"
            "  0% {{ opacity: 0; }}\n"
            "  {1}% {{ opacity: 1; }}\n"
            "  {2}% {{ opacity: 1; }}\n"
            "  100% {{ opacity: 0; }}\n"
            "}}\n"
            ".{3} {{ animation-name: {0}; animation-duration: {4}s; position: absolute; "
            "left: 10%; top: 10%; width: 80%; height: 80%; }}\n",
            kKeyframesName, hold_starts_pct, hold_ends_pct, kImageClass, total);

    const std::string xml =
            std::format(R"(<Canvas><Image class="{}" source="{}"/></Canvas>)", kImageClass, config.image.hex());

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

    return SplashDocument{std::move(*document), std::move(*stylesheet), reference_size};
}

}
