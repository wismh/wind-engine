#include "bench_app.h"
#include "bench_matrix.h"
#include "bench_options.h"

#include <engine/core/engine_host.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/profiler.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// Not ENGINE_GAME: the bench reads its command line, and returns its own exit code. This is the body of
// engine::Engine<GameT> with the app built from the parsed options.
int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    const auto options = bench::parse_bench_options(args);
    if (!options) {
        std::fprintf(stderr, "wind_ui_bench: %s\n\n%s", options.error().c_str(), bench::bench_usage().c_str());
        return 2;
    }
    if (options->help) {
        std::printf("%s", bench::bench_usage().c_str());
        return 0;
    }
    if (options->list) {
        for (const bench::BenchCase& bench_case : bench::bench_matrix()) {
            std::printf("%s\n", bench::bench_case_name(bench_case).c_str());
        }
        return 0;
    }
    const auto bench_case = bench::resolve_bench_case(*options);
    if (!bench_case) {
        std::fprintf(stderr, "wind_ui_bench: %s\n", bench_case.error().c_str());
        return 2;
    }
    if (!engine::ui::kUiProfilerBuilt) {
        std::fprintf(stderr, "wind_ui_bench: this build has no UI profiler (ENGINE_UI_PROFILER is defined in Debug and "
                             "RelWithDebInfo); build wind_ui_bench in RelWithDebInfo\n");
        return 3;
    }

    // The host before the app, so the app is destroyed first while the services it holds live.
    engine::EngineHost host;
    if (!host.init()) {
        return 1;
    }
    bench::BenchApp app(host.services(), *bench_case, *options);
    if (!host.open_primary(app.primary_window())) {
        return 1;
    }
    if (!host.load_catalog(host.assets_root())) {
        host.fatal().report("Failed to load the bench catalog");
        host.dispose();
        return 1;
    }
    host.attach_game(app);
    const int code = host.run(engine::RunHooks{
            .on_start = [&app] { app.on_start(); },
            .on_frame_end = {},
            .on_quit = [&app] { app.on_quit(); },
    });
    return code != 0 ? code : app.exit_code();
}
