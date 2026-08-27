#include "arc_paint.h"

#include <algorithm>
#include <numbers>

namespace bench {

void ArcPaint::paint(engine::ui::IDrawList& list, const engine::render::Rect& content) {
    const glm::vec2 center{content.x + content.w * 0.5f, content.y + content.h * 0.5f};
    const float radius = std::min(content.w, content.h) * 0.5f - 3.0f;
    list.arc(center, radius, 0.0f, 1.5f * std::numbers::pi_v<float>, glm::vec4{0.96f, 0.52f, 0.09f, 1.0f}, 3.0f);
}

}
