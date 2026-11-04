#include "net/input_lead.h"

#include <algorithm>
#include <cstdint>

namespace engine::net {

int InputLead::observe(Tick snapshot_tick, int slack, int target, Tick predicted) noexcept {
    if (hold_until_ != kNoTick && snapshot_tick < hold_until_) {
        return 0;
    }
    hold_until_ = kNoTick;
    // The reported input was for about `snapshot_tick + slack`: what the rate did after it does not show yet.
    const std::int64_t reported = static_cast<std::int64_t>(snapshot_tick) + slack;
    const double in_flight = reported > 0 ? gained_since(static_cast<Tick>(reported), predicted) : 0.0;
    const double shortfall = static_cast<double>(target - slack) - in_flight;
    if (shortfall > kSnapTicks) {
        if (++far_reports_ >= kSnapReports) {
            const int jump = static_cast<int>(shortfall + 0.5);
            error_ = 0.0;
            far_reports_ = 0;
            gained_at_.fill(gained_);
            hold_until_ = predicted + static_cast<Tick>(jump);
            return jump;
        }
    } else {
        far_reports_ = 0;
    }
    const double clamped = std::clamp(shortfall, -static_cast<double>(kSnapTicks), static_cast<double>(kSnapTicks));
    error_ += (clamped - error_) * kSmoothing;
    return 0;
}

void InputLead::advance(Tick predicted) noexcept {
    const double step = adjust();
    gained_ += step;
    gained_at_[predicted % kHistory] = gained_;
    // Between reports the estimate follows what the step just did.
    error_ -= step;
}

void InputLead::reset() noexcept {
    error_ = 0.0;
    gained_ = 0.0;
    gained_at_.fill(0.0);
    hold_until_ = kNoTick;
    far_reports_ = 0;
}

float InputLead::rate() const noexcept {
    return static_cast<float>(1.0 + adjust());
}

double InputLead::error() const noexcept {
    return error_;
}

double InputLead::gained_since(Tick tick, Tick predicted) const noexcept {
    if (tick >= predicted || predicted - tick >= kHistory) {
        return 0.0;
    }
    return gained_ - gained_at_[tick % kHistory];
}

double InputLead::adjust() const noexcept {
    return std::clamp(error_ * kGain, -kMaxRateAdjust, kMaxRateAdjust);
}

} // namespace engine::net
