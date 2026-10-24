#pragma once

#include <cstddef>
#include <cstdint>

namespace engine {

// Detects a body that arrives too slowly, like curl's --speed-limit / --speed-time: the average speed over a
// window of `seconds` must reach `min_bytes_per_second`, window after window. The caller feeds it the clock
// and the bytes received so far after every read.
class StallWatch {
public:
    StallWatch() = default;
    // Off when either limit is 0.
    StallWatch(std::size_t min_bytes_per_second, float seconds, double now)
        : min_bytes_per_second_(min_bytes_per_second)
        , seconds_(static_cast<double>(seconds))
        , window_start_(now) {}

    [[nodiscard]] bool stalled(double now, std::uint64_t received) {
        if (min_bytes_per_second_ == 0 || seconds_ <= 0.0) {
            return false;
        }
        const double elapsed = now - window_start_;
        if (elapsed < seconds_) {
            return false;
        }
        const double speed = static_cast<double>(received - window_received_) / elapsed;
        if (speed < static_cast<double>(min_bytes_per_second_)) {
            return true;
        }
        window_start_ = now;
        window_received_ = received;
        return false;
    }

private:
    std::size_t min_bytes_per_second_ = 0;
    double seconds_ = 0.0;
    double window_start_ = 0.0;
    std::uint64_t window_received_ = 0;
};

}
