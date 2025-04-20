#include "profiler_panel.h"

#include "profiler_chart.h"

#include <engine/ecs/world.h>
#include <engine/ui/profiler.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor {
namespace {

constexpr char kIdleHint[] = "Play the game to profile its UI.";
constexpr char kNotBuiltHint[] = "The UI profiler is not in this build. Use Debug or RelWithDebInfo.";
constexpr char kSelectedTitle[] = "Selected canvas";

constexpr std::string_view kStageNames[] = {"bindings", "stylesheets", "input", "layout", "motion", "paint"};
static_assert(std::size(kStageNames) == engine::ui::kProfilerStageCount);

struct StageNumbers {
    std::int64_t last = 0;
    double average = 0.0;
    std::int64_t max = 0;
};

template<typename Frame, typename Read>
StageNumbers stage_numbers(std::span<const Frame> frames, Read read) {
    StageNumbers numbers;
    if (frames.empty()) {
        return numbers;
    }
    std::int64_t sum = 0;
    for (const Frame& frame : frames) {
        const std::int64_t value = read(frame);
        sum += value;
        numbers.max = std::max(numbers.max, value);
    }
    numbers.last = read(frames.back());
    numbers.average = static_cast<double>(sum) / static_cast<double>(frames.size());
    return numbers;
}

std::string stage_line(std::string_view name, const StageNumbers& numbers, std::string_view extra) {
    const auto ms = [](double ns) { return ns / 1000000.0; };
    return std::format("{}  last {:.2f} ms  avg {:.2f} ms  max {:.2f} ms{}", name,
            ms(static_cast<double>(numbers.last)), ms(numbers.average), ms(static_cast<double>(numbers.max)), extra);
}

// Last, average, and max per stage over the ring, the element counts of the last frame, then the
// shared stages.
std::string stats_text(std::span<const engine::ui::ProfilerFrame> frames,
        std::span<const engine::ui::ProfilerSharedFrame> shared) {
    if (frames.empty()) {
        return "No frames yet";
    }
    const engine::ui::ProfilerFrame& last = frames.back();
    std::string text;
    for (std::size_t stage = 0; stage < engine::ui::kProfilerStageCount; ++stage) {
        const bool layout = stage == static_cast<std::size_t>(engine::ui::ProfilerStage::Layout);
        const std::string_view extra = layout && last.layout_skipped() ? "  skipped" : "";
        text += stage_line(kStageNames[stage],
                stage_numbers(frames, [stage](const engine::ui::ProfilerFrame& frame) { return frame.stage_ns[stage]; }),
                extra);
        text += '\n';
    }
    text += std::format("elements {}  generated {}\n\n", last.elements, last.generated);
    text += stage_line("begin frame",
            stage_numbers(shared, [](const engine::ui::ProfilerSharedFrame& frame) { return frame.begin_frame_ns; }),
            "");
    text += '\n';
    text += stage_line("commands",
            stage_numbers(shared, [](const engine::ui::ProfilerSharedFrame& frame) { return frame.commands_ns; }), "");
    return text;
}

std::vector<ChartColumn> canvas_columns(std::span<const engine::ui::ProfilerFrame> frames) {
    std::vector<ChartColumn> columns(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        std::copy(frames[i].stage_ns.begin(), frames[i].stage_ns.end(), columns[i].stages_ns.begin());
        columns[i].layout_skipped = frames[i].layout_skipped();
    }
    return columns;
}

std::vector<ChartColumn> shared_columns(std::span<const engine::ui::ProfilerSharedFrame> frames) {
    std::vector<ChartColumn> columns(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        columns[i].stages_ns[0] = frames[i].begin_frame_ns;
        columns[i].stages_ns[1] = frames[i].commands_ns;
    }
    return columns;
}

// The ceiling the chart scales to, from the tallest stacked column.
double columns_ceiling_ms(const std::vector<ChartColumn>& columns) {
    double max_ms = 0.0;
    for (const ChartColumn& column : columns) {
        double sum = 0.0;
        for (const std::int64_t ns : column.stages_ns) {
            sum += static_cast<double>(std::max<std::int64_t>(ns, 0)) / 1000000.0;
        }
        max_ms = std::max(max_ms, sum);
    }
    return chart_ceiling_ms(max_ms);
}

}

ProfilerPanel::ProfilerPanel() : view_model_(std::make_shared<ProfilerViewModel>()) {
    show_idle();
}

const std::shared_ptr<ProfilerViewModel>& ProfilerPanel::view_model() const {
    return view_model_;
}

void ProfilerPanel::attach(engine::ecs::World& game) {
    game_ = &game;
    engine::ui::set_ui_profiler_attached(game, true);
    pause_shown_ = engine::ui::profiler_paused(game);
    view_model_->pause = pause_shown_;
    view_model_->hint = std::string(engine::ui::kUiProfilerBuilt ? "" : kNotBuiltHint);
}

void ProfilerPanel::detach() {
    if (game_ != nullptr) {
        engine::ui::set_ui_profiler_attached(*game_, false);
    }
    game_ = nullptr;
    rows_.clear();
    pause_shown_ = false;
    show_idle();
}

bool ProfilerPanel::attached() const {
    return game_ != nullptr;
}

void ProfilerPanel::refresh() {
    if (game_ == nullptr) {
        return;
    }
    if constexpr (engine::ui::kUiProfilerBuilt) {
        show_rings();
    }
}

void ProfilerPanel::show_rings() {
    if (view_model_->pause.get() != pause_shown_) {
        engine::ui::set_profiler_paused(*game_, view_model_->pause.get());
    }
    pause_shown_ = engine::ui::profiler_paused(*game_);
    view_model_->pause = pause_shown_;

    const std::vector<engine::ui::ProfilerCanvas> canvases = engine::ui::profiler_canvases(*game_);
    std::vector<std::shared_ptr<ProfilerRowViewModel>> visible;
    visible.reserve(canvases.size());
    std::map<engine::ecs::Entity, std::shared_ptr<ProfilerRowViewModel>> kept;
    std::string selected_label;
    for (const engine::ui::ProfilerCanvas& canvas : canvases) {
        std::shared_ptr<ProfilerRowViewModel> slot;
        if (const auto it = rows_.find(canvas.canvas); it != rows_.end()) {
            slot = it->second;
        } else {
            slot = std::make_shared<ProfilerRowViewModel>(*this);
        }
        slot->show(canvas);
        if (canvas.selected) {
            selected_label = canvas.label;
        }
        visible.push_back(slot);
        kept.emplace(canvas.canvas, std::move(slot));
    }
    rows_ = std::move(kept);
    view_model_->canvases.set(std::move(visible));

    if (canvases.empty()) {
        view_model_->chartTitle = std::string(kSelectedTitle);
        view_model_->chart.set_columns({});
        view_model_->shared.set_columns({});
        view_model_->stats = std::string("No canvas");
        return;
    }
    const std::vector<engine::ui::ProfilerFrame> frames =
            engine::ui::profiler_frames(*game_, engine::ui::profiler_selected(*game_));
    const std::vector<engine::ui::ProfilerSharedFrame> shared = engine::ui::profiler_shared_frames(*game_);
    std::vector<ChartColumn> columns = canvas_columns(frames);
    view_model_->chartTitle =
            std::format("{}: {}  (scale {:g} ms)", kSelectedTitle, selected_label, columns_ceiling_ms(columns));
    view_model_->chart.set_columns(std::move(columns));
    view_model_->shared.set_columns(shared_columns(shared));
    view_model_->stats = stats_text(frames, shared);
}

void ProfilerPanel::select(engine::ecs::Entity canvas) {
    if (game_ != nullptr) {
        engine::ui::profiler_select(*game_, canvas);
    }
}

void ProfilerPanel::show_idle() {
    view_model_->canvases.set({});
    view_model_->chart.set_columns({});
    view_model_->shared.set_columns({});
    view_model_->pause = false;
    view_model_->chartTitle = std::string(kSelectedTitle);
    view_model_->stats = std::string();
    view_model_->hint = std::string(engine::ui::kUiProfilerBuilt ? kIdleHint : kNotBuiltHint);
}

}
