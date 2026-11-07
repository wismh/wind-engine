#pragma once

#include <string>

namespace engine::bench {

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
    bool passed_stability{true};
    std::string notes;
};

}
