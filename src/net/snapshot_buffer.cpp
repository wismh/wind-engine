#include "net/snapshot_buffer.h"

#include <engine/net/net_messages.h>

#include <algorithm>

namespace engine::net {

namespace {

[[nodiscard]] bool outside_window(Tick tick, Tick newest) noexcept {
    return newest >= kSnapshotWindowTicks && tick <= newest - kSnapshotWindowTicks;
}

} // namespace

bool SnapshotBuffer::insert(std::shared_ptr<const SnapshotState> state) {
    if (state == nullptr || state->tick == kNoTick || find(state->tick) != nullptr) {
        return false;
    }
    const Tick newest_tick = std::max(newest(), state->tick);
    if (outside_window(state->tick, newest_tick)) {
        return false;
    }
    const auto at = std::lower_bound(states_.begin(), states_.end(), state->tick,
            [](const std::shared_ptr<const SnapshotState>& held, Tick tick) { return held->tick < tick; });
    states_.insert(at, std::move(state));
    std::erase_if(states_, [newest_tick](const std::shared_ptr<const SnapshotState>& held) {
        return outside_window(held->tick, newest_tick);
    });
    return true;
}

const SnapshotState* SnapshotBuffer::find(Tick tick) const noexcept {
    const auto at = std::lower_bound(states_.begin(), states_.end(), tick,
            [](const std::shared_ptr<const SnapshotState>& held, Tick value) { return held->tick < value; });
    if (at == states_.end() || (*at)->tick != tick) {
        return nullptr;
    }
    return at->get();
}

Tick SnapshotBuffer::newest() const noexcept {
    return states_.empty() ? kNoTick : states_.back()->tick;
}

std::vector<Tick> SnapshotBuffer::ticks() const {
    std::vector<Tick> out;
    out.reserve(states_.size());
    for (const auto& state : states_) {
        out.push_back(state->tick);
    }
    return out;
}

bool SnapshotBuffer::empty() const noexcept {
    return states_.empty();
}

bool SnapshotBuffer::sample(double render_tick, const SnapshotState*& from, const SnapshotState*& to,
        float& t) const noexcept {
    // First state strictly after render_tick; the one before it is `from`.
    const auto after = std::upper_bound(states_.begin(), states_.end(), render_tick,
            [](double value, const std::shared_ptr<const SnapshotState>& held) {
                return value < static_cast<double>(held->tick);
            });
    if (after == states_.begin()) {
        return false;
    }
    from = std::prev(after)->get();
    if (after == states_.end()) {
        to = nullptr;
        t = 0.0f;
        return true;
    }
    to = after->get();
    const double span = static_cast<double>(to->tick) - static_cast<double>(from->tick);
    t = static_cast<float>(std::clamp((render_tick - static_cast<double>(from->tick)) / span, 0.0, 1.0));
    return true;
}

void SnapshotBuffer::clear() noexcept {
    states_.clear();
}

} // namespace engine::net
