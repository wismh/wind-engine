#include "bench_2d.h"
#include "bench_3d.h"
#include "bench_types.h"

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
              << "  --all         Run both 2D and 3D benchmarks (default)\n"
              << "  --2d          Run only 2D benchmarks\n"
              << "  --3d          Run only 3D benchmarks\n"
              << "  --steps N     Override step count for each scenario\n"
              << "  --threads N   Worker thread count for JobSystem (0 = serial)\n"
              << "  --help        Show this help message\n";
}

void print_table(const std::string& suite_title, const std::vector<engine::bench::BenchResult>& results) {
    std::cout << "\n### " << suite_title << " Benchmark Results\n\n";
    std::cout << "| Scenario | Steps | Bodies (Dyn/Stat) | Mean (us/step) | Min (us) | Max (us) | Total (ms) | Sleep "
                 "| Stability |\n";
    std::cout << "|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|\n";

    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(32) << r.name << " | " << std::right << std::setw(5) << r.steps
                  << " | " << r.dynamic_bodies << "/" << r.static_bodies << " | " << std::fixed << std::setprecision(1)
                  << std::setw(14) << r.mean_us << " | " << std::setw(8) << r.min_us << " | " << std::setw(8)
                  << r.max_us << " | " << std::setprecision(2) << std::setw(10) << r.total_ms << " | "
                  << r.sleeping_at_end << "/" << r.dynamic_bodies << " | "
                  << (r.passed_stability ? "OK" : "DRIFT/FAIL") << " |\n";
    }
    std::cout << std::endl;
}

}

int main(int argc, char* argv[]) {
    bool run_2d = false;
    bool run_3d = false;
    int steps_override = 0;
    std::uint32_t worker_threads = std::max(1u, std::thread::hardware_concurrency() - 1u);

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
        } else if (arg == "--steps" && i + 1 < argc) {
            steps_override = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            worker_threads = static_cast<std::uint32_t>(std::max(0, std::stoi(argv[++i])));
        }
    }

    if (!run_2d && !run_3d) {
        run_2d = true;
        run_3d = true;
    }

    std::cout << "========================================================\n"
              << "       Wind Engine Physics Benchmark Suite (CLI)        \n"
              << "       Worker Threads: " << worker_threads << "\n"
              << "========================================================\n";

    if (run_2d) {
        const auto results_2d = engine::bench::run_all_2d_benchmarks(steps_override, worker_threads);
        print_table("2D Physics", results_2d);
    }

    if (run_3d) {
        const auto results_3d = engine::bench::run_all_3d_benchmarks(steps_override, worker_threads);
        print_table("3D Physics", results_3d);
    }

    return 0;
}
