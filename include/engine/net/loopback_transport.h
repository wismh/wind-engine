#pragma once

#include <engine/net/loopback_bus.h>
#include <engine/net/network_transport.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace engine::net {

class LoopbackTransport final : public INetworkTransport {
public:
    explicit LoopbackTransport(LoopbackBus& bus);
    ~LoopbackTransport() override;

    LoopbackTransport(const LoopbackTransport&) = delete;
    LoopbackTransport& operator=(const LoopbackTransport&) = delete;

    bool start_server(std::uint16_t port) override;
    bool connect(std::string_view address, std::uint16_t port) override;
    void disconnect(PeerId peer_id) override;
    void stop() override;

    bool send(PeerId target, std::span<const std::uint8_t> data,
        DeliveryMode mode = DeliveryMode::ReliableOrdered, std::uint8_t channel = 0) override;

    void poll_events(const EventCallback& callback) override;

    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] bool is_server() const override;
    [[nodiscard]] float rtt_ms(PeerId peer_id) const override;

    bool accept_loopback_client(LoopbackTransport* client, PeerId& out_client_id);
    void receive_incoming_data(PeerId sender_id, std::span<const std::uint8_t> data);
    void notify_peer_disconnected(PeerId peer_id, DisconnectReason reason);

private:
    struct InternalEvent {
        TransportEventType type{TransportEventType::Disconnected};
        PeerId peer_id{kInvalidPeerId};
        std::vector<std::uint8_t> payload{};
        DisconnectReason reason{DisconnectReason::None};
    };

    struct PeerConnection {
        PeerId remote_peer_id{kInvalidPeerId};
        LoopbackTransport* endpoint{nullptr};
    };

    void enqueue_event(InternalEvent event);

    LoopbackBus& bus_;
    bool is_running_{false};
    bool is_server_{false};
    std::uint16_t port_{0};

    mutable std::mutex state_mutex_{};
    std::unordered_map<PeerId, PeerConnection> connections_{};
    PeerId next_peer_id_{kServerPeerId + 1};

    std::mutex queue_mutex_{};
    std::vector<InternalEvent> event_queue_{};
};

} // namespace engine::net
