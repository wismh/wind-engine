#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <engine/net/message_dispatcher.h>
#include <engine/net/net_payload_id.h>
#include <engine/net/net_payload_options.h>
#include <engine/net/net_payload_progress.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace engine::net {

// One peer's outgoing payloads, sent a fragment at a time as the budget allows. The highest priority plus age goes
// next, so a payload that keeps waiting ends up first; ties go to the older one.
class PayloadQueue {
public:
    using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

    struct Limits {
        double aging_per_tick{0.0};
        std::size_t backlog_bytes{0};
    };

    void push(PayloadId id, MessageId message, Bytes bytes, const NetPayloadOptions& options, Tick now);

    // Forgets `id`. A payload the client holds part of gets a PayloadCancel, so it can free the part.
    bool cancel(INetworkTransport& transport, PeerId peer, PayloadId id);
    bool set_priority(PayloadId id, std::int32_t priority) noexcept;
    [[nodiscard]] std::optional<NetPayloadProgress> progress(PayloadId id) const noexcept;
    // Bytes not yet handed to the transport.
    [[nodiscard]] std::size_t queued_bytes() const noexcept;
    // True when `id` had been sent whole and is now done.
    bool acknowledge(PayloadId id);

    // Sends fragments while `credit` covers the next one, the transport's backlog is under the limit, and it accepts
    // them. A refused fragment is sent again later. Takes what it sent out of `credit` and returns it.
    std::size_t pump(INetworkTransport& transport, PeerId peer, Tick now, const Limits& limits, double& credit);

    [[nodiscard]] std::vector<PayloadId> ids() const;

private:
    struct Item {
        PayloadId id{kNoPayload};
        MessageId message{0};
        Bytes bytes{};
        std::int32_t priority{0};
        bool compressed{false};
        Tick queued{kNoTick};
        std::size_t sent{0};
        bool started{false};
        bool awaiting_ack{false};
    };

    [[nodiscard]] Item* find(PayloadId id) noexcept;
    [[nodiscard]] Item* next_to_send(Tick now, double aging_per_tick) noexcept;

    std::vector<Item> items_{};
};

} // namespace engine::net
