#include "net/snapshot_buffer.h"

#include <engine/net/net_messages.h>

#include <algorithm>

namespace engine::net {

namespace {

[[nodiscard]] bool outside_window(Tick tick, Tick newest) noexcept {
    return newest >= kSnapshotWindowTicks && tick <= newest - kSnapshotWindowTicks;
}

} // namespace

bool SnapshotBuffer::insert(SnapshotDelta delta) {
    const Tick tick = delta.tick;
    if (tick == kNoTick || find(tick) != nullptr) {
        return false;
    }
    const Tick newest_tick = std::max(newest(), tick);
    if (outside_window(tick, newest_tick)) {
        return false;
    }
    const auto at = std::lower_bound(entries_.begin(), entries_.end(), tick,
            [](const Entry& held, Tick value) { return held.state->tick < value; });
    const SnapshotState* previous = at != entries_.begin() ? std::prev(at)->state.get() : base_.get();
    auto state = apply_snapshot(previous, delta, /*strict=*/true);
    if (!state.has_value()) {
        return false;
    }
    const auto inserted = entries_.insert(
            at, Entry{.state = std::make_shared<const SnapshotState>(std::move(*state)), .delta = std::move(delta)});

    // A late snapshot changes what every newer state started from.
    for (auto later = std::next(inserted); later != entries_.end(); ++later) {
        auto derived = apply_snapshot(std::prev(later)->state.get(), later->delta, /*strict=*/false);
        later->state = std::make_shared<const SnapshotState>(std::move(*derived));
    }

    while (!entries_.empty() && outside_window(entries_.front().state->tick, newest_tick)) {
        base_ = std::move(entries_.front().state);
        entries_.erase(entries_.begin());
    }
    return true;
}

const SnapshotState* SnapshotBuffer::find(Tick tick) const noexcept {
    const auto at = std::lower_bound(entries_.begin(), entries_.end(), tick,
            [](const Entry& held, Tick value) { return held.state->tick < value; });
    if (at == entries_.end() || at->state->tick != tick) {
        return nullptr;
    }
    return at->state.get();
}

Tick SnapshotBuffer::newest() const noexcept {
    return entries_.empty() ? kNoTick : entries_.back().state->tick;
}

std::vector<Tick> SnapshotBuffer::ticks() const {
    std::vector<Tick> out;
    out.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        out.push_back(entry.state->tick);
    }
    return out;
}

bool SnapshotBuffer::empty() const noexcept {
    return entries_.empty();
}

bool SnapshotBuffer::sample(double render_tick, const SnapshotState*& from, const SnapshotState*& to,
        float& t) const noexcept {
    // First state strictly after render_tick; the one before it is `from`.
    const auto after = std::upper_bound(entries_.begin(), entries_.end(), render_tick,
            [](double value, const Entry& held) { return value < static_cast<double>(held.state->tick); });
    if (after == entries_.begin()) {
        return false;
    }
    from = std::prev(after)->state.get();
    if (after == entries_.end()) {
        to = nullptr;
        t = 0.0f;
        return true;
    }
    to = after->state.get();
    const double span = static_cast<double>(to->tick) - static_cast<double>(from->tick);
    t = static_cast<float>(std::clamp((render_tick - static_cast<double>(from->tick)) / span, 0.0, 1.0));
    return true;
}

void SnapshotBuffer::clear() noexcept {
    entries_.clear();
    base_.reset();
}

} // namespace engine::net
