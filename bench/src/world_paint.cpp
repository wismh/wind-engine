#include "world_paint.h"

#include "hud_data.h"
#include "team_colors.h"

#include <cstddef>
#include <numbers>
#include <utility>

namespace bench {
namespace {

constexpr glm::vec4 kGridColor{1.0f, 1.0f, 1.0f, 0.08f};
constexpr glm::vec4 kSelectionColor{0.9f, 0.95f, 0.4f, 1.0f};
constexpr float kUnitSize = 8.0f;
constexpr std::size_t kSelected = 12;

}

WorldPaint::WorldPaint(std::vector<UnitMarker> units) : units_(std::move(units)) {}

void WorldPaint::paint(engine::ui::IDrawList& list, const engine::render::Rect& content) {
    const glm::vec2 origin{content.x, content.y};
    for (float x = 0.0f; x <= kWorldSize.x; x += kWorldGrid) {
        list.line(origin + glm::vec2{x, 0.0f}, origin + glm::vec2{x, kWorldSize.y}, kGridColor, 1.0f);
    }
    for (float y = 0.0f; y <= kWorldSize.y; y += kWorldGrid) {
        list.line(origin + glm::vec2{0.0f, y}, origin + glm::vec2{kWorldSize.x, y}, kGridColor, 1.0f);
    }
    for (const UnitMarker& unit : units_) {
        const glm::vec2 corner = origin + unit.position - glm::vec2{kUnitSize * 0.5f};
        list.fill_rect({corner.x, corner.y, kUnitSize, kUnitSize}, kTeamColors[static_cast<std::size_t>(unit.team)]);
    }
    for (std::size_t i = 0; i < kSelected && i < units_.size(); ++i) {
        list.arc(origin + units_[i].position, kUnitSize, 0.0f, 2.0f * std::numbers::pi_v<float>, kSelectionColor,
                1.5f);
    }
}

}
