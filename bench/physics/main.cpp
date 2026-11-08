#include "bench_2d.h"
#include "bench_3d.h"
#include "bench_types.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --all                 Run both 2D and 3D benchmarks (default)\n"
              << "  --2d                  Run only 2D benchmarks\n"
              << "  --3d                  Run only 3D benchmarks\n"
              << "  --filter <substr>     Run only scenarios matching <substr> (alias: --scenario)\n"
              << "  --skip-large          Skip the 5,050-box large_pyramid_100 scenario\n"
              << "  --full                Run full step counts on large scenarios\n"
              << "  --steps N             Override step count for each scenario\n"
              << "  --threads N           Worker thread count for JobSystem (0 = serial)\n"
              << "  --help                Show this help message\n";
}

void print_table(const std::string& suite_title, const std::vector<engine::bench::BenchResult>& results) {
    if (results.empty()) {
        return;
    }
    std::cout << "\n### " << suite_title << " Benchmark Results\n\n";
    std::cout << "| Scenario | Steps | Bodies (Dyn/Stat) | Mean (us/step) | Min (us) | Max (us) | Total (ms) "
              << "| Memory (KB) | Sleep (Steps) | Stability |\n";
    std::cout << "|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|\n";

    for (const auto& r : results) {
        const std::string bodies_col = std::to_string(r.dynamic_bodies) + "/" + std::to_string(r.static_bodies);
        const std::string sleep_col =
                std::to_string(r.sleeping_at_end) + "/" + std::to_string(r.dynamic_bodies) +
                (r.steps_to_sleep >= 0 ? (" (@" + std::to_string(r.steps_to_sleep) + ")") : " (-)");

        std::cout << "| " << std::left << std::setw(36) << r.name << " | " << std::right << std::setw(5) << r.steps
                  << " | " << std::setw(11) << bodies_col << " | " << std::fixed << std::setprecision(1)
                  << std::setw(14) << r.mean_us << " | " << std::setw(8) << r.min_us << " | " << std::setw(8)
                  << r.max_us << " | " << std::setprecision(2) << std::setw(10) << r.total_ms << " | "
                  << std::setw(11) << r.approx_memory_kb << " | " << std::setw(15) << sleep_col << " | "
                  << (r.passed_stability ? "OK" : "DRIFT/FAIL") << " |\n";
    }
    std::cout << std::endl;
}

}

int main(int argc, char* argv[]) {
    bool run_2d = false;
    bool run_3d = false;
    engine::bench::BenchOptions options{
            .worker_threads = std::max(1u, std::thread::hardware_concurrency() - 1u),
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--2d") {
            run_2d = true;
        } else if (arg == "--3d") {
            run_3d = true;
        } else if (arg == "--all") {
            run_2d = true;
            run_3d = true;
        } else if (arg == "--skip-large") {
            options.skip_large = true;
        } else if (arg == "--full" || arg == "-f") {
            options.full_steps = true;
        } else if ((arg == "--filter" || arg == "--scenario") && i + 1 < argc) {
            options.filter = argv[++i];
        } else if (arg.starts_with("--filter=")) {
            options.filter = arg.substr(9);
        } else if (arg.starts_with("--scenario=")) {
            options.filter = arg.substr(11);
        } else if (arg == "--steps" && i + 1 < argc) {
            options.steps_override = std::stoi(argv[++i]);
        } else if (arg.starts_with("--steps=")) {
            options.steps_override = std::stoi(arg.substr(8));
        } else if (arg == "--threads" && i + 1 < argc) {
            options.worker_threads = static_cast<std::uint32_t>(std::max(0, std::stoi(argv[++i])));
        } else if (arg.starts_with("--threads=")) {
            options.worker_threads = static_cast<std::uint32_t>(std::max(0, std::stoi(arg.substr(10))));
        }
    }

    if (!run_2d && !run_3d) {
        run_2d = true;
        run_3d = true;
    }

    std::cout << "========================================================\n"
              << "       Wind Engine Physics Benchmark Suite (CLI)        \n"
              << "       Worker Threads: " << options.worker_threads << "\n";
    if (!options.filter.empty()) {
        std::cout << "       Filter: \"" << options.filter << "\"\n";
    }
    std::cout << "========================================================\n";

    if (run_2d) {
        const auto results_2d = engine::bench::run_all_2d_benchmarks(options);
        print_table("2D Physics (Box2D v3 Comparative Suite)", results_2d);
    }

    if (run_3d) {
        const auto results_3d = engine::bench::run_all_3d_benchmarks(options);
        print_table("3D Physics", results_3d);
    }

    return 0;
}
