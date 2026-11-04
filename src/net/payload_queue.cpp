#include "net/payload_queue.h"

#include <engine/net/byte_stream.h>
#include <engine/net/net_messages.h>

#include <algorithm>
#include <span>

namespace engine::net {

namespace {

constexpr std::uint8_t kPayloadFlagCompressed = 1u << 0;
constexpr std::size_t kBeginHeaderBytes = 2u + 4u + 2u + 1u + 4u;
constexpr std::size_t kFragmentHeaderBytes = 2u + 4u + 4u;

} // namespace

PayloadQueue::Item* PayloadQueue::find(PayloadId id) noexcept {
    const auto at = std::find_if(items_.begin(), items_.end(), [id](const Item& item) { return item.id == id; });
    return at != items_.end() ? &*at : nullptr;
}

void PayloadQueue::push(PayloadId id, MessageId message, Bytes bytes, const NetPayloadOptions& options, Tick now) {
    items_.push_back(Item{.id = id,
            .message = message,
            .bytes = std::move(bytes),
            .priority = options.priority,
            .compressed = options.compressed,
            .queued = now});
}

bool PayloadQueue::cancel(INetworkTransport& transport, PeerId peer, PayloadId id) {
    Item* item = find(id);
    if (item == nullptr) {
        return false;
    }
    if (item->started && !item->awaiting_ack) {
        ByteWriter message(2u + 4u);
        message.write_u16(kPayloadCancelMessageId);
        message.write_u32(id);
        (void)transport.send(peer, message.bytes(), DeliveryMode::ReliableOrdered);
    }
    std::erase_if(items_, [id](const Item& held) { return held.id == id; });
    return true;
}

bool PayloadQueue::set_priority(PayloadId id, std::int32_t priority) noexcept {
    Item* item = find(id);
    if (item == nullptr) {
        return false;
    }
    item->priority = priority;
    return true;
}

std::optional<NetPayloadProgress> PayloadQueue::progress(PayloadId id) const noexcept {
    const auto at = std::find_if(items_.begin(), items_.end(), [id](const Item& item) { return item.id == id; });
    if (at == items_.end()) {
        return std::nullopt;
    }
    return NetPayloadProgress{
            .sent_bytes = at->sent, .total_bytes = at->bytes->size(), .awaiting_ack = at->awaiting_ack};
}

std::size_t PayloadQueue::queued_bytes() const noexcept {
    std::size_t total = 0;
    for (const Item& item : items_) {
        total += item.bytes->size() - item.sent;
    }
    return total;
}

bool PayloadQueue::acknowledge(PayloadId id) {
    const Item* item = find(id);
    if (item == nullptr || !item->awaiting_ack) {
        return false;
    }
    std::erase_if(items_, [id](const Item& held) { return held.id == id; });
    return true;
}

PayloadQueue::Item* PayloadQueue::next_to_send(Tick now, double aging_per_tick) noexcept {
    Item* best = nullptr;
    double best_priority = 0.0;
    for (Item& item : items_) {
        if (item.awaiting_ack) {
            continue;
        }
        const double priority = static_cast<double>(item.priority) + static_cast<double>(now - item.queued) * aging_per_tick;
        if (best == nullptr || priority > best_priority || (priority == best_priority && item.id < best->id)) {
            best = &item;
            best_priority = priority;
        }
    }
    return best;
}

std::size_t PayloadQueue::pump(
        INetworkTransport& transport, PeerId peer, Tick now, const Limits& limits, double& credit) {
    std::size_t total = 0;
    while (true) {
        Item* item = next_to_send(now, limits.aging_per_tick);
        if (item == nullptr) {
            break;
        }
        const std::size_t size = item->bytes->size();
        const std::size_t data = std::min(size - item->sent, kPayloadFragmentBytes);
        const std::size_t message_size = data + (item->started ? kFragmentHeaderBytes : kBeginHeaderBytes);
        if (static_cast<double>(message_size) > credit || transport.pending_send_bytes(peer) >= limits.backlog_bytes) {
            break;
        }
        ByteWriter message(message_size);
        if (!item->started) {
            message.write_u16(kPayloadBeginMessageId);
            message.write_u32(item->id);
            message.write_u16(item->message);
            message.write_u8(item->compressed ? kPayloadFlagCompressed : std::uint8_t{0});
            message.write_u32(static_cast<std::uint32_t>(size));
        } else {
            message.write_u16(kPayloadFragmentMessageId);
            message.write_u32(item->id);
            message.write_u32(static_cast<std::uint32_t>(item->sent));
        }
        message.append(std::span(*item->bytes).subspan(item->sent, data));
        if (!transport.send(peer, message.bytes(), DeliveryMode::ReliableOrdered)) {
            break;
        }
        item->started = true;
        item->sent += data;
        item->awaiting_ack = item->sent == size;
        credit -= static_cast<double>(message_size);
        total += message_size;
    }
    return total;
}

std::vector<PayloadId> PayloadQueue::ids() const {
    std::vector<PayloadId> out;
    out.reserve(items_.size());
    for (const Item& item : items_) {
        out.push_back(item.id);
    }
    return out;
}

} // namespace engine::net
