#pragma once

#include <engine/render/commands.h>
#include <engine/ui/profiler.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace editor {

// Pure geometry of the Profiler panel's stacked frame charts. No drawing and no engine state, so
// wind_editor_tests checks it directly.
//
// One slot per frame of the profiler's ring. Stored frames sit on the right; the left stays empty
// until the ring fills. Stage 0 is the bottom of a stacked column.
constexpr int kChartSlots = engine::ui::kProfilerRingFrames;
constexpr int kChartStageCap = 8;
constexpr double kChartBudgetMs = 16.7;
constexpr float kLayoutSkipTickPx = 2.0f;

struct ChartColumn {
    std::array<std::int64_t, kChartStageCap> stages_ns{};
    bool layout_skipped = false;
};

enum class ChartMark : std::uint8_t { Stage, LayoutSkip };

struct ChartRect {
    engine::render::Rect rect{};
    int stage = 0;
    ChartMark mark = ChartMark::Stage;
};

struct ChartBudgetLine {
    glm::vec2 from{};
    glm::vec2 to{};
    bool visible = false;
};

struct ChartGeometry {
    std::vector<ChartRect> rects;
    ChartBudgetLine budget{};
    double ceiling_ms = 1.0;
};

// Smallest of 1, 2, 4, 8, … ms that covers `max_ms`. Never below 1 ms.
[[nodiscard]] inline double chart_ceiling_ms(double max_ms) {
    double ceiling = 1.0;
    while (ceiling < max_ms && ceiling < 1.0e6) {
        ceiling *= 2.0;
    }
    return ceiling;
}

// Stacked columns in content space (origin top-left, y down). `layout_ticks` draws a 2px mark at
// the bottom of a column whose layout was skipped. The budget line is drawn when the ceiling is
// above one 60 fps frame.
[[nodiscard]] inline ChartGeometry build_chart(std::span<const ChartColumn> frames, int stage_count, float width,
                                               float height, bool layout_ticks) {
    ChartGeometry geometry;
    if (stage_count <= 0 || stage_count > kChartStageCap || width <= 0.0f || height <= 0.0f || frames.empty()) {
        return geometry;
    }

    const int count = std::min(static_cast<int>(frames.size()), kChartSlots);
    const std::size_t begin = frames.size() - static_cast<std::size_t>(count);
    double max_ms = 0.0;
    for (int i = 0; i < count; ++i) {
        const ChartColumn &column = frames[begin + static_cast<std::size_t>(i)];
        double sum = 0.0;
        for (int stage = 0; stage < stage_count; ++stage) {
            const std::int64_t ns = column.stages_ns[static_cast<std::size_t>(stage)];
            if (ns > 0) {
                sum += static_cast<double>(ns) / 1000000.0;
            }
        }
        max_ms = std::max(max_ms, sum);
    }
    geometry.ceiling_ms = chart_ceiling_ms(max_ms);

    const float slot_w = width / static_cast<float>(kChartSlots);
    const int origin = kChartSlots - count;
    geometry.rects.reserve(static_cast<std::size_t>(count) * static_cast<std::size_t>(stage_count));
    for (int i = 0; i < count; ++i) {
        const ChartColumn &column = frames[begin + static_cast<std::size_t>(i)];
        const float x = static_cast<float>(origin + i) * slot_w;
        float top = height;
        for (int stage = 0; stage < stage_count; ++stage) {
            const std::int64_t ns = column.stages_ns[static_cast<std::size_t>(stage)];
            if (ns <= 0) {
                continue;
            }
            const double ms = static_cast<double>(ns) / 1000000.0;
            const float bar = static_cast<float>(ms / geometry.ceiling_ms) * height;
            if (bar <= 0.0f) {
                continue;
            }
            top -= bar;
            ChartRect rect;
            rect.rect = {x, top, slot_w, bar};
            rect.stage = stage;
            rect.mark = ChartMark::Stage;
            geometry.rects.push_back(rect);
        }
        if (layout_ticks && column.layout_skipped) {
            const float tick = std::min(kLayoutSkipTickPx, height);
            ChartRect rect;
            rect.rect = {x, height - tick, slot_w, tick};
            rect.mark = ChartMark::LayoutSkip;
            geometry.rects.push_back(rect);
        }
    }

    if (geometry.ceiling_ms > kChartBudgetMs) {
        const float y = height * static_cast<float>(1.0 - kChartBudgetMs / geometry.ceiling_ms);
        geometry.budget.visible = true;
        geometry.budget.from = {0.0f, y};
        geometry.budget.to = {width, y};
    }
    return geometry;
}

}
