#pragma once

// docs/tech/features/Windowing.md#frame-pacing

#include <chrono>
#include <optional>

namespace engine {

// Software frame cap for ticks no vsync swap waits on (every window hidden or minimized, or no swap
// interval). Keeps frames `period` apart on a fixed schedule, so a short sleep that overshoots does not
// lower the rate. A frame more than one period late starts a new schedule from that frame instead of
// running frames back to back to catch up.
class FrameLimiter {
public:
    using Clock = std::chrono::steady_clock;

    // How long to wait after the frame that ended at `now`. Zero for the first frame and for a late one.
    [[nodiscard]] std::chrono::nanoseconds wait_after(Clock::time_point now, std::chrono::nanoseconds period);

private:
    std::optional<Clock::time_point> deadline_;
};

}
