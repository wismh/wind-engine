#pragma once

#include "bench_case.h"

#include <engine/ui/profiler.h>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace bench {

inline constexpr int kDefaultWarmup = 60;
// The bench looks the scene's layout up on tick kSetupTick (3), so the warmup covers that and at least one frame of
// the mode before the rings are cleared.
inline constexpr int kMinWarmup = 5;
inline constexpr int kMinWindowSide = 320;
inline constexpr int kMaxWindowSide = 8192;

// The command line: `--scene <name> --mode <name> [--variant <name>] [--warmup N] [--frames N] [--out path.json]
// [--width N --height N]`, `--list`, or `--help`.
struct BenchOptions {
    bool list = false;
    bool help = false;
    std::string scene;
    std::string mode;
    std::string variant;
    int warmup = kDefaultWarmup;
    // Measured frames. The default is the ring size, so the profiler's ring is exactly the measured window.
    int frames = engine::ui::kProfilerRingFrames;
    // Empty: default_report_path of the case, in the working directory.
    std::filesystem::path out;
    int width = 1600;
    int height = 900;
};

// `args` without the program name. An unknown flag, a flag without its value, a number that does not parse or is out
// of range, or a missing --scene or --mode (unless --list or --help) is an error naming the problem.
[[nodiscard]] std::expected<BenchOptions, std::string> parse_bench_options(std::span<const std::string_view> args);

// The matrix row the options name. The error says what is wrong and lists what is valid there.
[[nodiscard]] std::expected<BenchCase, std::string> resolve_bench_case(const BenchOptions& options);

// `ui_bench-table-hover-paint.json`.
[[nodiscard]] std::filesystem::path default_report_path(const BenchCase& bench_case);

[[nodiscard]] std::string bench_usage();

}
