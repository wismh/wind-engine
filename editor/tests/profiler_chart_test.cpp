#include <gtest/gtest.h>

#include "profiler_chart.h"
#include "profiler_chart_paint.h"

#include <engine/ui/draw_list.h>

#include <span>
#include <string_view>
#include <vector>

namespace {

const editor::ChartRect* find_stage(const editor::ChartGeometry& geometry, int stage) {
    for (const editor::ChartRect& rect : geometry.rects) {
        if (rect.mark == editor::ChartMark::Stage && rect.stage == stage) {
            return &rect;
        }
    }
    return nullptr;
}

class CountingDrawList final : public engine::ui::IDrawList {
public:
    int fills = 0;
    int lines = 0;

    void line(glm::vec2, glm::vec2, glm::vec4, float) override {
        ++lines;
    }
    void fill_rect(const engine::render::Rect&, glm::vec4, float) override {
        ++fills;
    }
    void stroke_rect(const engine::render::Rect&, glm::vec4, float, float) override {}
    void arc(glm::vec2, float, float, float, glm::vec4, float) override {}
    void set_font(engine::AssetId, float) override {}
    void text(std::string_view, glm::vec2, glm::vec4) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
};

}

TEST(ProfilerChart, CeilingAndBudgetLine) {
    EXPECT_DOUBLE_EQ(editor::chart_ceiling_ms(0.1), 1.0);
    EXPECT_DOUBLE_EQ(editor::chart_ceiling_ms(2.0), 2.0);
    EXPECT_DOUBLE_EQ(editor::chart_ceiling_ms(20.0), 32.0);

    editor::ChartColumn quiet{};
    quiet.stages_ns[0] = 100000;
    const editor::ChartGeometry quiet_geo =
            editor::build_chart(std::span<const editor::ChartColumn>(&quiet, 1), 1, 120.0f, 100.0f, false);
    EXPECT_DOUBLE_EQ(quiet_geo.ceiling_ms, 1.0);
    const editor::ChartRect* bar = find_stage(quiet_geo, 0);
    ASSERT_NE(bar, nullptr);
    EXPECT_FLOAT_EQ(bar->rect.h, 10.0f);
    EXPECT_FALSE(quiet_geo.budget.visible);

    editor::ChartColumn spike{};
    spike.stages_ns[0] = 20000000;
    const editor::ChartGeometry spike_geo =
            editor::build_chart(std::span<const editor::ChartColumn>(&spike, 1), 1, 120.0f, 64.0f, false);
    EXPECT_DOUBLE_EQ(spike_geo.ceiling_ms, 32.0);
    ASSERT_TRUE(spike_geo.budget.visible);
    const float y = 64.0f * static_cast<float>(1.0 - 16.7 / 32.0);
    EXPECT_FLOAT_EQ(spike_geo.budget.from.y, y);
    EXPECT_FLOAT_EQ(spike_geo.budget.to.y, y);
    EXPECT_FLOAT_EQ(spike_geo.budget.from.x, 0.0f);
    EXPECT_FLOAT_EQ(spike_geo.budget.to.x, 120.0f);
}

TEST(ProfilerChart, StacksTheLastSlotAndMarksASkippedLayout) {
    editor::ChartColumn column{};
    column.stages_ns[3] = 2000000;
    const editor::ChartGeometry stacked =
            editor::build_chart(std::span<const editor::ChartColumn>(&column, 1), 6, 120.0f, 100.0f, true);
    EXPECT_DOUBLE_EQ(stacked.ceiling_ms, 2.0);
    const editor::ChartRect* layout = find_stage(stacked, 3);
    ASSERT_NE(layout, nullptr);
    EXPECT_FLOAT_EQ(layout->rect.x, 119.0f);
    EXPECT_FLOAT_EQ(layout->rect.w, 1.0f);
    EXPECT_FLOAT_EQ(layout->rect.h, 100.0f);
    EXPECT_FLOAT_EQ(layout->rect.y, 0.0f);
    EXPECT_EQ(find_stage(stacked, 0), nullptr);
    EXPECT_FALSE(stacked.budget.visible);

    editor::ChartColumn skipped{};
    skipped.layout_skipped = true;
    const editor::ChartGeometry tick =
            editor::build_chart(std::span<const editor::ChartColumn>(&skipped, 1), 6, 120.0f, 100.0f, true);
    EXPECT_EQ(find_stage(tick, 3), nullptr);
    ASSERT_EQ(tick.rects.size(), 1u);
    EXPECT_EQ(tick.rects[0].mark, editor::ChartMark::LayoutSkip);
    EXPECT_FLOAT_EQ(tick.rects[0].rect.h, 2.0f);
    EXPECT_FLOAT_EQ(tick.rects[0].rect.y, 98.0f);
    EXPECT_FLOAT_EQ(tick.rects[0].rect.x, 119.0f);
}

TEST(ProfilerChart, PaintDrawsTheColumnsThroughTheDrawList) {
    editor::ProfilerChartPaint paint{editor::ProfilerChartPaint::canvas_colors(), true};
    CountingDrawList empty;
    paint.paint(empty, {0.0f, 0.0f, 120.0f, 100.0f});
    EXPECT_EQ(empty.fills, 0) << "an empty ring draws no columns";

    std::vector<editor::ChartColumn> columns(2);
    columns[0].stages_ns[0] = 1000000;
    columns[0].stages_ns[5] = 1000000;
    columns[1].stages_ns[3] = 30000000;
    columns[1].layout_skipped = true;
    paint.set_columns(columns);
    CountingDrawList list;
    paint.paint(list, {0.0f, 0.0f, 120.0f, 100.0f});
    EXPECT_EQ(list.fills, 4) << "two stages, one stage, one layout-skip tick";
    EXPECT_EQ(list.lines, 1) << "the 60 fps budget line above a 32 ms ceiling";
}
