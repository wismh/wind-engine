#include "core/frame_limiter.h"

namespace engine {

std::chrono::nanoseconds FrameLimiter::wait_after(Clock::time_point now, std::chrono::nanoseconds period) {
    if (!deadline_ || now >= *deadline_ + period) {
        deadline_ = now + period;
        return std::chrono::nanoseconds::zero();
    }
    const Clock::time_point deadline = *deadline_;
    *deadline_ += period;
    if (now >= deadline) {
        return std::chrono::nanoseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now);
}

}
