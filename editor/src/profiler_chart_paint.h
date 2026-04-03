#pragma once

#include "profiler_chart.h"

#include <engine/ui/paint.h>

#include <glm/vec4.hpp>

#include <vector>

namespace editor {

// A stacked frame chart as an IPaint on the profiler view-model. The panel hands it the columns during
// the editor's Game phase; paint only draws them, so nothing reads the game world while the editor paints.
class ProfilerChartPaint final : public engine::ui::IPaint {
public:
    // One color per stage, bottom first. `layout_ticks` marks frames that skipped layout.
    ProfilerChartPaint(std::vector<glm::vec4> stage_colors, bool layout_ticks);

    void set_columns(std::vector<ChartColumn> columns);
    [[nodiscard]] const std::vector<ChartColumn>& columns() const;

    void paint(engine::ui::IDrawList& list, const engine::render::Rect& content) override;

    // Stage colors of the canvas chart (bindings, stylesheets, input, layout, motion, paint) and the
    // shared chart (begin frame, commands). assets/css/panels.css repeats the first six for its legend.
    [[nodiscard]] static std::vector<glm::vec4> canvas_colors();
    [[nodiscard]] static std::vector<glm::vec4> shared_colors();

private:
    std::vector<glm::vec4> colors_;
    bool layout_ticks_;
    std::vector<ChartColumn> columns_;
};

}
