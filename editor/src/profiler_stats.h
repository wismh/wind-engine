#pragma once

#include <engine/ui/profiler.h>

#include <span>
#include <string>

namespace editor {

// The Profiler tab's numbers: last, average, and max per stage over the selected canvas's ring, `skipped` after
// layout when the last frame skipped it, the element counts and the draw calls (last, average, max), the painter
// calls of the last frame that are not zero, then the shared stages. "No frames yet" for an empty ring.
[[nodiscard]] std::string profiler_stats_text(std::span<const engine::ui::ProfilerFrame> frames,
        std::span<const engine::ui::ProfilerSharedFrame> shared);

}
