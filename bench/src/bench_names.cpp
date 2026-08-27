#include "bench_mode.h"
#include "bench_scene.h"

namespace bench {

std::string_view bench_scene_name(BenchScene scene) {
    switch (scene) {
        case BenchScene::Table:
            return "table";
        case BenchScene::Inspector:
            return "inspector";
        case BenchScene::Hud:
            return "hud";
        case BenchScene::PaintMix:
            return "paint-mix";
        case BenchScene::Motion:
            return "motion";
        case BenchScene::Text:
            return "text";
        case BenchScene::Clip:
            return "clip";
    }
    return "";
}

std::optional<BenchScene> find_bench_scene(std::string_view name) {
    for (const BenchScene scene : kBenchScenes) {
        if (bench_scene_name(scene) == name) {
            return scene;
        }
    }
    return std::nullopt;
}

std::string_view bench_mode_name(BenchMode mode) {
    switch (mode) {
        case BenchMode::Quiet:
            return "quiet";
        case BenchMode::OneChange:
            return "one-change";
        case BenchMode::Hover:
            return "hover";
        case BenchMode::Scroll:
            return "scroll";
        case BenchMode::Churn:
            return "churn";
    }
    return "";
}

std::optional<BenchMode> find_bench_mode(std::string_view name) {
    for (const BenchMode mode : kBenchModes) {
        if (bench_mode_name(mode) == name) {
            return mode;
        }
    }
    return std::nullopt;
}

}
