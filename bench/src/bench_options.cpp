#include "bench_options.h"

#include "bench_matrix.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <system_error>
#include <vector>

namespace bench {
namespace {

std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

template<typename T>
std::string joined(const std::vector<T>& items, std::string_view (*name)(T)) {
    std::string out;
    for (const T& item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name(item);
    }
    return out;
}

std::string joined(const std::vector<std::string_view>& items) {
    std::string out;
    for (const std::string_view item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
    }
    return out;
}

std::string all_scenes() {
    return joined(std::vector<BenchScene>(kBenchScenes.begin(), kBenchScenes.end()), &bench_scene_name);
}

}

std::expected<BenchOptions, std::string> parse_bench_options(std::span<const std::string_view> args) {
    BenchOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view flag = args[i];
        if (flag == "--list") {
            options.list = true;
            continue;
        }
        if (flag == "--help" || flag == "-h") {
            options.help = true;
            continue;
        }
        if (i + 1 >= args.size()) {
            if (flag.starts_with("--")) {
                return std::unexpected(std::format("{} needs a value", flag));
            }
            return std::unexpected(std::format("unknown argument '{}'", flag));
        }
        const std::string_view value = args[i + 1];
        const auto number = [&](int min, int max) -> std::expected<int, std::string> {
            const std::optional<int> parsed = parse_int(value);
            if (!parsed || *parsed < min || *parsed > max) {
                return std::unexpected(std::format("{} takes a whole number from {} to {}, not '{}'", flag, min, max,
                        value));
            }
            return *parsed;
        };
        std::expected<int, std::string> parsed_number{0};
        if (flag == "--scene") {
            options.scene = value;
        } else if (flag == "--mode") {
            options.mode = value;
        } else if (flag == "--variant") {
            options.variant = value;
        } else if (flag == "--out") {
            options.out = std::filesystem::path(std::u8string(value.begin(), value.end()));
        } else if (flag == "--warmup") {
            parsed_number = number(kMinWarmup, 100000);
            options.warmup = parsed_number.value_or(0);
        } else if (flag == "--frames") {
            parsed_number = number(1, engine::ui::kProfilerRingFrames);
            options.frames = parsed_number.value_or(0);
        } else if (flag == "--width") {
            parsed_number = number(kMinWindowSide, kMaxWindowSide);
            options.width = parsed_number.value_or(0);
        } else if (flag == "--height") {
            parsed_number = number(kMinWindowSide, kMaxWindowSide);
            options.height = parsed_number.value_or(0);
        } else {
            return std::unexpected(std::format("unknown argument '{}'", flag));
        }
        if (!parsed_number) {
            return std::unexpected(parsed_number.error());
        }
        ++i;
    }
    if (!options.list && !options.help && (options.scene.empty() || options.mode.empty())) {
        return std::unexpected(std::string("--scene and --mode are required (or --list)"));
    }
    return options;
}

std::expected<BenchCase, std::string> resolve_bench_case(const BenchOptions& options) {
    const std::optional<BenchScene> scene = find_bench_scene(options.scene);
    if (!scene) {
        return std::unexpected(std::format("unknown scene '{}'. Scenes: {}", options.scene, all_scenes()));
    }
    const std::vector<BenchMode> modes = bench_modes_of(*scene);
    const std::optional<BenchMode> mode = find_bench_mode(options.mode);
    if (!mode || std::ranges::find(modes, *mode) == modes.end()) {
        return std::unexpected(std::format("scene {} has no mode '{}'. Modes: {}", options.scene, options.mode,
                joined(modes, &bench_mode_name)));
    }
    const std::vector<std::string_view> variants = bench_variants_of(*scene, *mode);
    if (const std::optional<BenchCase> found = find_bench_case(*scene, *mode, options.variant)) {
        return *found;
    }
    if (variants.empty()) {
        return std::unexpected(
                std::format("{} {} has no variants, drop --variant {}", options.scene, options.mode, options.variant));
    }
    if (options.variant.empty()) {
        return std::unexpected(std::format("{} {} needs --variant: {}", options.scene, options.mode, joined(variants)));
    }
    return std::unexpected(std::format("{} {} has no variant '{}'. Variants: {}", options.scene, options.mode,
            options.variant, joined(variants)));
}

std::filesystem::path default_report_path(const BenchCase& bench_case) {
    std::string name =
            std::format("ui_bench-{}-{}", bench_scene_name(bench_case.scene), bench_mode_name(bench_case.mode));
    if (!bench_case.variant.empty()) {
        name += '-';
        name += bench_case.variant;
    }
    name += ".json";
    return std::filesystem::path(name);
}

std::string bench_usage() {
    return std::format(
            "wind_ui_bench --scene <name> --mode <name> [--variant <name>] [--warmup N] [--frames N]\n"
            "              [--out path.json] [--width N --height N]\n"
            "wind_ui_bench --list\n"
            "\n"
            "Renders one UI scene in one mode, records the UI profiler for --frames frames after --warmup\n"
            "frames, writes the snapshot with the run's metadata to --out, and exits.\n"
            "  --warmup   frames before the rings are cleared (default {}, at least {})\n"
            "  --frames   measured frames, 1 to the ring size {} (default {})\n"
            "  --out      report path (default ui_bench-<scene>-<mode>[-<variant>].json)\n"
            "  --width, --height   window size (default 1600 x 900)\n"
            "  --list     every scene, mode, and variant, one per line\n"
            "Scenes: {}\n",
            kDefaultWarmup, kMinWarmup, engine::ui::kProfilerRingFrames, engine::ui::kProfilerRingFrames, all_scenes());
}

}
