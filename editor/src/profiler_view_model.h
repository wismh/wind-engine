#pragma once

#include "profiler_chart_paint.h"
#include "profiler_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/profiler.xml. Names are the XML binding paths, so they stay camelCase.
class ProfilerViewModel final : public engine::ui::ViewModel {
public:
    ProfilerViewModel();

    engine::ui::BindableList<std::shared_ptr<ProfilerRowViewModel>> canvases;
    // Two-way with the game world's profiler Pause.
    engine::ui::Bindable<bool> pause;
    engine::ui::Bindable<std::string> hint;
    engine::ui::Bindable<std::string> chartTitle;
    engine::ui::Bindable<std::string> stats;
    // `paint` bindings. The generated bind() does not register paints; the constructor does.
    ProfilerChartPaint chart{ProfilerChartPaint::canvas_colors(), true};
    ProfilerChartPaint shared{ProfilerChartPaint::shared_colors(), false};
};

}
