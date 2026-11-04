#pragma once

// docs/tech/modules/Net.md

#include <engine/net/net_tick.h>

#include <array>
#include <cstddef>

namespace engine::net {

// Keeps a predicting client's tick ahead of the server by `target` ticks of input slack. The server reports,
// in each snapshot, how many ticks early this client's inputs arrived; the client runs its fixed step up to 10%
// fast or slow (`rate`) to bring that to the target. A report is a round trip old, so the rate change made since
// the reported input was sent is added to it; otherwise the loop swings around the target on a slow link.
// `kSnapReports` reports in a row more than `kSnapTicks` short jump the client tick forward instead; a single one
// (a burst after a lag spike) only counts as `kSnapTicks`. It never jumps backward: a client too far ahead only
// slows down.
class InputLead {
public:
    static constexpr double kMaxRateAdjust = 0.1;
    static constexpr double kGain = 0.05;
    static constexpr double kSmoothing = 0.2;
    static constexpr int kSnapTicks = 8;
    static constexpr int kSnapReports = 2;

    // A report from the snapshot at `snapshot_tick`, newer than any before, while the client predicts
    // `predicted`. Returns how many ticks to jump forward (0: none). After a jump, reports from snapshots before
    // the new predicted tick are ignored: they predate the jump.
    [[nodiscard]] int observe(Tick snapshot_tick, int slack, int target, Tick predicted) noexcept;
    // The fixed step for `predicted` ran at `rate()`.
    void advance(Tick predicted) noexcept;
    void reset() noexcept;

    // The fixed-step rate to run at, 0.9..1.1.
    [[nodiscard]] float rate() const noexcept;
    // The estimated shortfall in ticks now (positive: inputs arrive too close to their tick).
    [[nodiscard]] double error() const noexcept;

private:
    static constexpr std::size_t kHistory = 256;

    // The ticks of lead gained or lost by the rate after the step for `tick`, as far as the history reaches.
    [[nodiscard]] double gained_since(Tick tick, Tick predicted) const noexcept;
    [[nodiscard]] double adjust() const noexcept;

    double error_{0.0};
    // The sum of every step's rate change, and its value after the step for each recent tick.
    double gained_{0.0};
    std::array<double, kHistory> gained_at_{};
    Tick hold_until_{kNoTick};
    int far_reports_{0};
};

} // namespace engine::net
