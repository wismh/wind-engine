#include "profiler_chart_paint.h"

#include <utility>

namespace editor {
namespace {

constexpr glm::vec4 rgb(int r, int g, int b, float a = 1.0f) {
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, a};
}

// .sw-skip in assets/css/panels.css.
constexpr glm::vec4 kLayoutSkipColor = rgb(92, 99, 112);
constexpr glm::vec4 kBudgetColor = rgb(213, 219, 228, 0.85f);

}

ProfilerChartPaint::ProfilerChartPaint(std::vector<glm::vec4> stage_colors, bool layout_ticks)
    : colors_(std::move(stage_colors))
    , layout_ticks_(layout_ticks) {}

void ProfilerChartPaint::set_columns(std::vector<ChartColumn> columns) {
    columns_ = std::move(columns);
}

const std::vector<ChartColumn>& ProfilerChartPaint::columns() const {
    return columns_;
}

void ProfilerChartPaint::paint(engine::ui::IDrawList& list, const engine::render::Rect& content) {
    const ChartGeometry geometry = build_chart(columns_, static_cast<int>(colors_.size()), content.w, content.h,
            layout_ticks_);
    for (const ChartRect& mark : geometry.rects) {
        if (mark.rect.w <= 0.0f || mark.rect.h <= 0.0f) {
            continue;
        }
        const glm::vec4 color =
                mark.mark == ChartMark::LayoutSkip ? kLayoutSkipColor : colors_[static_cast<std::size_t>(mark.stage)];
        list.fill_rect(mark.rect, color);
    }
    if (geometry.budget.visible) {
        list.line(geometry.budget.from, geometry.budget.to, kBudgetColor, 1.0f);
    }
}

std::vector<glm::vec4> ProfilerChartPaint::canvas_colors() {
    return {rgb(122, 162, 247), rgb(187, 154, 247), rgb(224, 175, 104), rgb(247, 118, 142), rgb(158, 206, 106),
            rgb(125, 207, 255)};
}

std::vector<glm::vec4> ProfilerChartPaint::shared_colors() {
    return {rgb(192, 202, 245), rgb(255, 158, 100)};
}

}
