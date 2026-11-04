#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace engine::net {

using PeerId = std::uint64_t;
inline constexpr PeerId kInvalidPeerId = 0;
inline constexpr PeerId kServerPeerId = 1;

// No mode orders unreliable messages: a receiver of `Unreliable` or `UnreliableImmediate` data orders it
// itself (snapshots carry their tick). `UnreliableImmediate` skips the transport's batching delay (GNS Nagle).
enum class DeliveryMode : std::uint8_t {
    Unreliable,
    UnreliableImmediate,
    ReliableOrdered,
};

enum class TransportEventType : std::uint8_t {
    Connected,
    Disconnected,
    DataReceived,
};

enum class DisconnectReason : std::uint8_t {
    None,
    ClosedByPeer,
    Timeout,
    ConnectionFailed,
    Kicked,
};

struct TransportEvent {
    TransportEventType type{TransportEventType::Disconnected};
    PeerId peer_id{kInvalidPeerId};
    std::span<const std::uint8_t> payload{};
    DisconnectReason reason{DisconnectReason::None};
};

class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;

    virtual bool start_server(std::uint16_t port) = 0;
    virtual bool connect(std::string_view address, std::uint16_t port) = 0;
    virtual void disconnect(PeerId peer_id) = 0;
    virtual void stop() = 0;

    virtual bool send(PeerId target, std::span<const std::uint8_t> data,
        DeliveryMode mode = DeliveryMode::ReliableOrdered, std::uint8_t channel = 0) = 0;

    using EventCallback = std::function<void(const TransportEvent&)>;
    virtual void poll_events(const EventCallback& callback) = 0;

    [[nodiscard]] virtual bool is_running() const = 0;
    [[nodiscard]] virtual bool is_server() const = 0;
    [[nodiscard]] virtual float rtt_ms(PeerId peer_id) const = 0;
    // Bytes handed to `send` for this peer that are not on the wire yet, reliable retransmits included. A new message
    // waits behind them.
    [[nodiscard]] virtual std::size_t pending_send_bytes(PeerId peer_id) const = 0;
};

} // namespace engine::net
