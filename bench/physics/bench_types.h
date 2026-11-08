#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::bench {

struct BenchOptions {
    int steps_override{0};
    std::uint32_t worker_threads{0};
    std::string filter{};
    bool skip_large{false};
    bool full_steps{false};
};

struct BenchResult {
    std::string name;
    int steps{0};
    int total_entities{0};
    int dynamic_bodies{0};
    int static_bodies{0};
    double total_ms{0.0};
    double mean_us{0.0};
    double min_us{0.0};
    double max_us{0.0};
    int sleeping_at_end{0};
    int steps_to_sleep{-1};
    std::size_t approx_memory_kb{0};
    bool passed_stability{true};
    std::string notes;
};

}
