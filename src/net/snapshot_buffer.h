#pragma once

// docs/tech/modules/Net.md

#include "net/snapshot_state.h"

#include <engine/net/net_tick.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace engine::net {

// The client's received snapshots, full states sorted by tick. They are both the interpolation buffer and the
// delta baselines. A state at least `kSnapshotWindowTicks` behind the newest is dropped; the server never
// uses a baseline that old, so both sides agree on which baselines exist.
class SnapshotBuffer {
public:
    // False for a tick already held or one outside the window.
    bool insert(std::shared_ptr<const SnapshotState> state);

    [[nodiscard]] const SnapshotState* find(Tick tick) const noexcept;
    [[nodiscard]] Tick newest() const noexcept;
    [[nodiscard]] std::vector<Tick> ticks() const;
    [[nodiscard]] bool empty() const noexcept;

    // `from` is the newest state at or before `render_tick`, `to` the one after it (null when there is none:
    // the newest holds, nothing is extrapolated) and `t` the position between them. False when no state is at
    // or before `render_tick` yet.
    [[nodiscard]] bool sample(double render_tick, const SnapshotState*& from, const SnapshotState*& to,
            float& t) const noexcept;

    void clear() noexcept;

private:
    std::vector<std::shared_ptr<const SnapshotState>> states_{};
};

} // namespace engine::net
