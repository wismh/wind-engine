#pragma once

#include "bench_case.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

// The hover rule of the reference scenes: `paint` changes only a color, `layout` changes the padding.
inline constexpr std::string_view kHoverPaint = "paint";
inline constexpr std::string_view kHoverLayout = "layout";

// Every combination the bench runs, reference scenes first. `--list` prints it and the command line is checked
// against it.
[[nodiscard]] std::span<const BenchCase> bench_matrix();

// `table hover paint`, `hud quiet`: the scene, the mode, and the variant when there is one.
[[nodiscard]] std::string bench_case_name(const BenchCase& bench_case);

// The matrix row of that scene, mode, and variant (empty when the pair has none), or nullopt.
[[nodiscard]] std::optional<BenchCase> find_bench_case(BenchScene scene, BenchMode mode, std::string_view variant);

// The modes a scene supports, in matrix order.
[[nodiscard]] std::vector<BenchMode> bench_modes_of(BenchScene scene);

// The variants of a scene and mode, in matrix order. Empty when the pair has none.
[[nodiscard]] std::vector<std::string_view> bench_variants_of(BenchScene scene, BenchMode mode);

}
