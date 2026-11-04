#pragma once

#if defined(ENGINE_WITH_GNS)

#include <engine/net/network_transport.h>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::net {

class GnsBaseTransport {
public:
    explicit GnsBaseTransport(bool use_steam_id);
    virtual ~GnsBaseTransport();

    GnsBaseTransport(const GnsBaseTransport&) = delete;
    GnsBaseTransport& operator=(const GnsBaseTransport&) = delete;

    void disconnect(PeerId peer_id);
    void stop();

    bool send(PeerId target, std::span<const std::uint8_t> data, DeliveryMode mode, std::uint8_t channel);
    void poll_events(const INetworkTransport::EventCallback& callback);

    [[nodiscard]] bool is_running() const;
    [[nodiscard]] bool is_server() const;
    [[nodiscard]] float rtt_ms(PeerId peer_id) const;
    [[nodiscard]] std::size_t pending_send_bytes(PeerId peer_id) const;

    void on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info);

protected:
    bool setup_server_sockets(HSteamListenSocket listen_socket);
    bool setup_client_connection(HSteamNetConnection client_conn);

    ISteamNetworkingSockets* sockets_{nullptr};
    HSteamListenSocket listen_socket_{k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup poll_group_{k_HSteamNetPollGroup_Invalid};
    HSteamNetConnection client_conn_{k_HSteamNetConnection_Invalid};

    bool is_server_{false};
    bool is_running_{false};
    bool use_steam_id_{false};

private:
    struct QueuedEvent {
        TransportEventType type{TransportEventType::Disconnected};
        PeerId peer_id{kInvalidPeerId};
        std::vector<std::uint8_t> payload{};
        DisconnectReason reason{DisconnectReason::None};
    };

    void enqueue(QueuedEvent ev);

    mutable std::mutex state_mutex_{};
    std::unordered_map<PeerId, HSteamNetConnection> peer_to_conn_{};
    std::unordered_map<HSteamNetConnection, PeerId> conn_to_peer_{};
    std::vector<QueuedEvent> event_queue_{};
};

} // namespace engine::net

#endif
