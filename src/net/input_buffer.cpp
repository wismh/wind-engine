#include "net/input_buffer.h"

#include <engine/net/net_messages.h>

#include <utility>

namespace engine::net {

InputBuffer::InputBuffer(Tick resolved) noexcept
    : resolved_(resolved) {}

InputBuffer::Insert InputBuffer::insert(Tick tick, std::span<const std::uint8_t> bytes, Tick server_tick) {
    if (tick == kNoTick) {
        return Insert::Late;
    }
    if (tick <= resolved_) {
        const Tick back = resolved_ - tick;
        if (back < 64 && (real_mask_ & (std::uint64_t{1} << back)) != 0) {
            return Insert::Duplicate;
        }
        return Insert::Late;
    }
    if (tick > server_tick + kMaxInputLeadTicks) {
        return Insert::TooFarAhead;
    }
    if (pending_.contains(tick)) {
        return Insert::Duplicate;
    }
    pending_.emplace(tick, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (newest_ == kNoTick || tick > newest_) {
        newest_ = tick;
    }
    return Insert::Stored;
}

InputBuffer::Taken InputBuffer::take(Tick tick) {
    while (!pending_.empty() && pending_.begin()->first < tick) {
        pending_.erase(pending_.begin());
    }
    const Tick advanced = tick > resolved_ ? tick - resolved_ : 0;
    real_mask_ = advanced >= 64 ? 0 : real_mask_ << advanced;
    resolved_ = tick;

    Taken taken;
    const auto it = pending_.find(tick);
    if (it != pending_.end()) {
        last_ = std::move(it->second);
        has_last_ = true;
        pending_.erase(it);
        real_mask_ |= 1u;
        taken.real = true;
    } else {
        taken.repeated = has_last_;
    }
    taken.bytes = last_;
    return taken;
}

Tick InputBuffer::newest() const noexcept {
    return newest_;
}

std::size_t InputBuffer::pending() const noexcept {
    return pending_.size();
}

} // namespace engine::net
