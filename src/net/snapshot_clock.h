#pragma once

// docs/tech/modules/Net.md

namespace engine::net {

// The client's estimate, in seconds, of the server time of the newest snapshot as of now. It runs with the
// frame clock and is pulled toward each new snapshot's time by running up to 5% fast or slow, so it never
// steps backward because of jitter. An error above `kSnapSeconds` (a stall, a server restart) snaps it.
class SnapshotClock {
public:
    static constexpr double kSnapSeconds = 0.25;
    static constexpr double kMaxRateAdjust = 0.05;
    static constexpr double kGain = 2.0;
    static constexpr double kSmoothing = 0.1;

    // A snapshot newer than any before it, produced at `server_seconds`.
    void observe(double server_seconds) noexcept;
    void advance(double dt) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] double now() const noexcept;
    // The smoothed difference between arriving snapshots and the clock.
    [[nodiscard]] double error() const noexcept;

private:
    double time_{0.0};
    double error_{0.0};
    bool started_{false};
};

} // namespace engine::net
