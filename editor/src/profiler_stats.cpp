#include "profiler_stats.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

namespace editor {
namespace {

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

// `paint` then each kind the last frame called, as `name count`.
std::string paint_line(const engine::ui::ProfilerFrame& last) {
    std::string line = "paint";
    for (std::size_t kind = 0; kind < engine::ui::kProfilerPaintKindCount; ++kind) {
        if (last.paint_commands[kind] == 0) {
            continue;
        }
        line += std::format("  {} {}",
                engine::ui::profiler_paint_kind_name(static_cast<engine::ui::ProfilerPaintKind>(kind)),
                last.paint_commands[kind]);
    }
    return line;
}

}

std::string profiler_stats_text(std::span<const engine::ui::ProfilerFrame> frames,
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
    text += std::format("elements {}  generated {}\n", last.elements, last.generated);
    const StageNumbers draws = stage_numbers(
            frames, [](const engine::ui::ProfilerFrame& frame) { return std::int64_t{frame.draw_calls}; });
    text += std::format("draw calls  last {}  avg {:.1f}  max {}\n", draws.last, draws.average, draws.max);
    text += paint_line(last);
    text += "\n\n";
    text += stage_line("begin frame",
            stage_numbers(shared, [](const engine::ui::ProfilerSharedFrame& frame) { return frame.begin_frame_ns; }),
            "");
    text += '\n';
    text += stage_line("commands",
            stage_numbers(shared, [](const engine::ui::ProfilerSharedFrame& frame) { return frame.commands_ns; }), "");
    return text;
}

}
