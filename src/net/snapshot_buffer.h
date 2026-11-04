#pragma once

// docs/tech/modules/Net.md

#include "net/snapshot_codec.h"
#include "net/snapshot_state.h"

#include <engine/net/net_tick.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace engine::net {

// The client's received snapshots, sorted by tick. Each state is the state before it with that snapshot's records on
// top, so whatever order they arrive in, values and existence are last-write-wins by tick: a snapshot arriving late is
// put in its place and the states after it are derived again. A state at least `kSnapshotWindowTicks` behind the
// newest is dropped; what it knew lives on in the states after it.
class SnapshotBuffer {
public:
    // False for a tick already held or outside the window, or a delta that does not fit the state before it. The
    // server rewrites what an unacknowledged snapshot carried, so refusing one loses nothing.
    bool insert(SnapshotDelta delta);

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
    struct Entry {
        std::shared_ptr<const SnapshotState> state{};
        SnapshotDelta delta{};
    };

    std::vector<Entry> entries_{};
    // The newest state that left the window: where the oldest entry starts from when it is derived again.
    std::shared_ptr<const SnapshotState> base_{};
};

} // namespace engine::net
