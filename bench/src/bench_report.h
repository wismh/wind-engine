#pragma once

#include "bench_case.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace bench {

// What a run measured besides the profiler snapshot: the case, the protocol, and the build.
struct BenchReport {
    BenchCase bench_case;
    int warmup = 0;
    int frames = 0;
    // The window size asked for, and the drawable size the canvas laid out in.
    int width = 0;
    int height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    bool vsync = false;
    // Interactive elements the hover mode cycles through; 0 in other modes.
    std::size_t hover_targets = 0;
    // The build configuration (RelWithDebInfo for reference numbers), the engine version, its build id, and the git
    // commit at configure time with whether the tree had changes. The commit is empty when git was not available.
    std::string config;
    std::string engine_version;
    std::string build_id;
    std::string commit;
    bool dirty = false;
    // UTC, ISO 8601 (utc_timestamp).
    std::string timestamp;
};

// `{"bench":{...metadata...},"profile":<profile>}`. `profile` is the engine's profiler_json; empty is written as null.
[[nodiscard]] std::string bench_report_json(const BenchReport& report, std::string_view profile);

// `2026-10-09T14:03:27Z`.
[[nodiscard]] std::string utc_timestamp(std::chrono::system_clock::time_point time);

}
