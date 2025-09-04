#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bench {

// One UI scene the bench can render. The first three are the UI Performance Plan's baseline scenes; the rest are
// diagnostic (one painter call kind, animation, text, clipping).
enum class BenchScene : std::uint8_t {
    Table,
    Inspector,
    Hud,
    PaintMix,
    Motion,
    Text,
    Clip,
};

inline constexpr std::array<BenchScene, 7> kBenchScenes{
        BenchScene::Table,
        BenchScene::Inspector,
        BenchScene::Hud,
        BenchScene::PaintMix,
        BenchScene::Motion,
        BenchScene::Text,
        BenchScene::Clip,
};

// The name on the command line and in the report.
[[nodiscard]] std::string_view bench_scene_name(BenchScene scene);

[[nodiscard]] std::optional<BenchScene> find_bench_scene(std::string_view name);

}
