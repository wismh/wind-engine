#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bench {

// What changes from one measured frame to the next.
enum class BenchMode : std::uint8_t {
    // Nothing: the view-model is still and the pointer rests on a spot that is not interactive.
    Quiet,
    // One bound value changes every frame.
    OneChange,
    // The pointer moves onto a different interactive element every frame.
    Hover,
    // A few pixels of wheel scroll every frame.
    Scroll,
    // A structural edit every frame: a row in and a row out, or a section collapsed or expanded.
    Churn,
};

inline constexpr std::array<BenchMode, 5> kBenchModes{
        BenchMode::Quiet,
        BenchMode::OneChange,
        BenchMode::Hover,
        BenchMode::Scroll,
        BenchMode::Churn,
};

[[nodiscard]] std::string_view bench_mode_name(BenchMode mode);

[[nodiscard]] std::optional<BenchMode> find_bench_mode(std::string_view name);

}
