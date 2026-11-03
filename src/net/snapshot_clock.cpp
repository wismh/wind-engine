#include "net/snapshot_clock.h"

#include <algorithm>
#include <cmath>

namespace engine::net {

void SnapshotClock::observe(double server_seconds) noexcept {
    if (!started_) {
        time_ = server_seconds;
        error_ = 0.0;
        started_ = true;
        return;
    }
    const double error = server_seconds - time_;
    if (std::abs(error) > kSnapSeconds) {
        time_ = server_seconds;
        error_ = 0.0;
        return;
    }
    error_ += (error - error_) * kSmoothing;
}

void SnapshotClock::advance(double dt) noexcept {
    if (!started_ || dt <= 0.0) {
        return;
    }
    const double adjust = std::clamp(error_ * kGain, -kMaxRateAdjust, kMaxRateAdjust);
    time_ += dt * (1.0 + adjust);
    // The clock closed part of the gap it measured.
    error_ -= dt * adjust;
}

void SnapshotClock::reset() noexcept {
    time_ = 0.0;
    error_ = 0.0;
    started_ = false;
}

bool SnapshotClock::started() const noexcept {
    return started_;
}

double SnapshotClock::now() const noexcept {
    return time_;
}

double SnapshotClock::error() const noexcept {
    return error_;
}

} // namespace engine::net
