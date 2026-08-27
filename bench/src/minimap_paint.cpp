#include "minimap_paint.h"

#include "hud_data.h"
#include "team_colors.h"

#include <cstddef>
#include <utility>

namespace bench {
namespace {

constexpr glm::vec4 kGround{0.11f, 0.16f, 0.11f, 1.0f};
constexpr glm::vec4 kCameraColor{1.0f, 1.0f, 1.0f, 0.8f};
// The part of the world the Viewport shows at 1600 x 900: 1280 x 656 px from the origin.
constexpr glm::vec2 kCameraView{1280.0f, 656.0f};

}

MinimapPaint::MinimapPaint(std::vector<UnitMarker> units) : units_(std::move(units)) {}

void MinimapPaint::paint(engine::ui::IDrawList& list, const engine::render::Rect& content) {
    const glm::vec2 scale{content.w / kWorldSize.x, content.h / kWorldSize.y};
    list.fill_rect(content, kGround);
    for (const UnitMarker& unit : units_) {
        const glm::vec2 dot = glm::vec2{content.x, content.y} + unit.position * scale;
        list.fill_rect({dot.x - 1.0f, dot.y - 1.0f, 2.0f, 2.0f}, kTeamColors[static_cast<std::size_t>(unit.team)]);
    }
    list.stroke_rect({content.x, content.y, kCameraView.x * scale.x, kCameraView.y * scale.y}, kCameraColor, 1.0f);
}

}
