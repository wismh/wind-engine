#include "profiler_row_view_model.h"

#include "profiler_panel.h"

#include <asset_ids.h>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kPlainFill[] = "#00000000";

}

ProfilerRowViewModel::ProfilerRowViewModel(ProfilerPanel& panel) : panel_(&panel) {
    assets::ui::Profiler::Canvases::bind(*this);
    select.bind_to<ProfilerRowViewModel, &ProfilerRowViewModel::select_row>(*this);
}

void ProfilerRowViewModel::show(const engine::ui::ProfilerCanvas& canvas) {
    canvas_ = canvas.canvas;
    label = canvas.label;
    rowFill = std::string(canvas.selected ? kSelectedFill : kPlainFill);
}

engine::ecs::Entity ProfilerRowViewModel::canvas() const {
    return canvas_;
}

void ProfilerRowViewModel::select_row() {
    panel_->select(canvas_);
}

}
