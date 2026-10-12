#include <engine/net/gns_transport.h>

#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace engine::net {

namespace {

std::atomic<int> s_gns_ref_count{0};
std::mutex s_registry_mutex{};

struct GnsInstanceRegistry {
    std::unordered_map<HSteamListenSocket, GnsTransport::Impl*> listeners{};
    std::unordered_map<HSteamNetConnection, GnsTransport::Impl*> connections{};
};

GnsInstanceRegistry s_registry{};

void global_connection_status_changed_callback(SteamNetConnectionStatusChangedCallback_t* info);

bool ensure_gns_initialized() {
    if (s_gns_ref_count.fetch_add(1) == 0) {
        SteamDatagramErrMsg err_msg{};
        if (!GameNetworkingSockets_Init(nullptr, err_msg)) {
            s_gns_ref_count.fetch_sub(1);
            return false;
        }
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(
            global_connection_status_changed_callback);
    }
    return true;
}

void release_gns() {
    if (s_gns_ref_count.fetch_sub(1) == 1) {
        GameNetworkingSockets_Kill();
    }
}

} // namespace

struct GnsTransport::Impl {
    ISteamNetworkingSockets* sockets{nullptr};
    HSteamListenSocket listen_socket{k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup poll_group{k_HSteamNetPollGroup_Invalid};
    HSteamNetConnection client_conn{k_HSteamNetConnection_Invalid};

    bool is_server{false};
    bool is_running{false};

    mutable std::mutex queue_mutex{};
    struct QueuedEvent {
        TransportEventType type{TransportEventType::Disconnected};
        PeerId peer_id{kInvalidPeerId};
        std::vector<std::uint8_t> payload{};
        DisconnectReason reason{DisconnectReason::None};
    };
    std::vector<QueuedEvent> event_queue{};

    void enqueue(QueuedEvent ev) {
        std::lock_guard lock(queue_mutex);
        event_queue.push_back(std::move(ev));
    }

    void on_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
        if (sockets == nullptr) {
            return;
        }

        switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting: {
            if (is_server) {
                if (sockets->AcceptConnection(info->m_hConn) != k_EResultOK) {
                    sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
                    return;
                }
                sockets->SetConnectionPollGroup(info->m_hConn, poll_group);
                {
                    std::lock_guard lock(s_registry_mutex);
                    s_registry.connections[info->m_hConn] = this;
                }
            }
            break;
        }
        case k_ESteamNetworkingConnectionState_Connected: {
            if (is_server) {
                enqueue(QueuedEvent{
                    .type = TransportEventType::Connected,
                    .peer_id = static_cast<PeerId>(info->m_hConn),
                    .payload = {},
                    .reason = DisconnectReason::None,
                });
            } else {
                enqueue(QueuedEvent{
                    .type = TransportEventType::Connected,
                    .peer_id = kServerPeerId,
                    .payload = {},
                    .reason = DisconnectReason::None,
                });
            }
            break;
        }
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            DisconnectReason reason = DisconnectReason::ClosedByPeer;
            if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                reason = DisconnectReason::ConnectionFailed;
            }

            if (is_server) {
                enqueue(QueuedEvent{
                    .type = TransportEventType::Disconnected,
                    .peer_id = static_cast<PeerId>(info->m_hConn),
                    .payload = {},
                    .reason = reason,
                });
            } else {
                enqueue(QueuedEvent{
                    .type = TransportEventType::Disconnected,
                    .peer_id = kServerPeerId,
                    .payload = {},
                    .reason = reason,
                });
            }

            {
                std::lock_guard lock(s_registry_mutex);
                s_registry.connections.erase(info->m_hConn);
            }
            sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
            if (!is_server && info->m_hConn == client_conn) {
                client_conn = k_HSteamNetConnection_Invalid;
                is_running = false;
            }
            break;
        }
        default:
            break;
        }
    }
};

namespace {

void global_connection_status_changed_callback(SteamNetConnectionStatusChangedCallback_t* info) {
    GnsTransport::Impl* target = nullptr;
    {
        std::lock_guard lock(s_registry_mutex);
        if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
            auto it = s_registry.listeners.find(info->m_info.m_hListenSocket);
            if (it != s_registry.listeners.end()) {
                target = it->second;
            }
        }
        if (target == nullptr) {
            auto it = s_registry.connections.find(info->m_hConn);
            if (it != s_registry.connections.end()) {
                target = it->second;
            }
        }
    }

    if (target != nullptr) {
        target->on_status_changed(info);
    }
}

} // namespace

GnsTransport::GnsTransport()
    : impl_(std::make_unique<Impl>()) {
    if (ensure_gns_initialized()) {
        impl_->sockets = SteamNetworkingSockets();
    }
}

GnsTransport::~GnsTransport() {
    stop();
    release_gns();
}

bool GnsTransport::start_server(std::uint16_t port) {
    if (impl_->is_running || impl_->sockets == nullptr) {
        return false;
    }

    SteamNetworkingIPAddr addr{};
    addr.Clear();
    addr.m_port = port;

    impl_->listen_socket = impl_->sockets->CreateListenSocketIP(addr, 0, nullptr);
    if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
        return false;
    }

    impl_->poll_group = impl_->sockets->CreatePollGroup();
    if (impl_->poll_group == k_HSteamNetPollGroup_Invalid) {
        impl_->sockets->CloseListenSocket(impl_->listen_socket);
        impl_->listen_socket = k_HSteamListenSocket_Invalid;
        return false;
    }

    {
        std::lock_guard lock(s_registry_mutex);
        s_registry.listeners[impl_->listen_socket] = impl_.get();
    }

    impl_->is_server = true;
    impl_->is_running = true;
    return true;
}

bool GnsTransport::connect(std::string_view address, std::uint16_t port) {
    if (impl_->is_running || impl_->sockets == nullptr) {
        return false;
    }

    SteamNetworkingIPAddr addr{};
    addr.Clear();
    const std::string addr_str(address);
    if (!addr.ParseString(addr_str.c_str())) {
        return false;
    }
    addr.m_port = port;

    impl_->client_conn = impl_->sockets->ConnectByIPAddress(addr, 0, nullptr);
    if (impl_->client_conn == k_HSteamNetConnection_Invalid) {
        return false;
    }

    {
        std::lock_guard lock(s_registry_mutex);
        s_registry.connections[impl_->client_conn] = impl_.get();
    }

    impl_->is_server = false;
    impl_->is_running = true;
    return true;
}

void GnsTransport::disconnect(PeerId peer_id) {
    if (!impl_->is_running || impl_->sockets == nullptr) {
        return;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (impl_->is_server) {
        conn = static_cast<HSteamNetConnection>(peer_id);
    } else {
        conn = impl_->client_conn;
    }

    if (conn != k_HSteamNetConnection_Invalid) {
        {
            std::lock_guard lock(s_registry_mutex);
            s_registry.connections.erase(conn);
        }
        impl_->sockets->CloseConnection(conn, 0, nullptr, false);
        if (!impl_->is_server) {
            impl_->client_conn = k_HSteamNetConnection_Invalid;
            impl_->is_running = false;
        }
    }
}

void GnsTransport::stop() {
    if (!impl_->is_running || impl_->sockets == nullptr) {
        return;
    }

    if (impl_->is_server) {
        if (impl_->listen_socket != k_HSteamListenSocket_Invalid) {
            {
                std::lock_guard lock(s_registry_mutex);
                s_registry.listeners.erase(impl_->listen_socket);
            }
            impl_->sockets->CloseListenSocket(impl_->listen_socket);
            impl_->listen_socket = k_HSteamListenSocket_Invalid;
        }

        if (impl_->poll_group != k_HSteamNetPollGroup_Invalid) {
            impl_->sockets->DestroyPollGroup(impl_->poll_group);
            impl_->poll_group = k_HSteamNetPollGroup_Invalid;
        }
    } else {
        if (impl_->client_conn != k_HSteamNetConnection_Invalid) {
            {
                std::lock_guard lock(s_registry_mutex);
                s_registry.connections.erase(impl_->client_conn);
            }
            impl_->sockets->CloseConnection(impl_->client_conn, 0, nullptr, false);
            impl_->client_conn = k_HSteamNetConnection_Invalid;
        }
    }

    impl_->is_running = false;
}

bool GnsTransport::send(PeerId target, std::span<const std::uint8_t> data,
    DeliveryMode mode, std::uint8_t channel) {
    (void)channel;
    if (!impl_->is_running || impl_->sockets == nullptr) {
        return false;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (impl_->is_server) {
        conn = static_cast<HSteamNetConnection>(target);
    } else {
        conn = impl_->client_conn;
    }

    if (conn == k_HSteamNetConnection_Invalid) {
        return false;
    }

    int send_flags = k_nSteamNetworkingSend_Reliable;
    switch (mode) {
    case DeliveryMode::Unreliable:
        send_flags = k_nSteamNetworkingSend_Unreliable;
        break;
    case DeliveryMode::UnreliableSequenced:
        send_flags = k_nSteamNetworkingSend_UnreliableNoNagle;
        break;
    case DeliveryMode::ReliableOrdered:
        send_flags = k_nSteamNetworkingSend_Reliable;
        break;
    }

    const EResult res = impl_->sockets->SendMessageToConnection(
        conn, data.data(), static_cast<uint32>(data.size()), send_flags, nullptr);
    return res == k_EResultOK;
}

void GnsTransport::poll_events(const EventCallback& callback) {
    if (impl_->sockets == nullptr) {
        return;
    }

    impl_->sockets->RunCallbacks();

    if (impl_->is_running) {
        constexpr int kMaxMessages = 64;
        SteamNetworkingMessage_t* msgs[kMaxMessages];

        if (impl_->is_server && impl_->poll_group != k_HSteamNetPollGroup_Invalid) {
            int num = impl_->sockets->ReceiveMessagesOnPollGroup(impl_->poll_group, msgs, kMaxMessages);
            while (num > 0) {
                for (int i = 0; i < num; ++i) {
                    SteamNetworkingMessage_t* msg = msgs[i];
                    const TransportEvent ev{
                        .type = TransportEventType::DataReceived,
                        .peer_id = static_cast<PeerId>(msg->m_conn),
                        .payload = std::span<const std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(msg->m_pData), msg->m_cbSize),
                        .reason = DisconnectReason::None,
                    };
                    callback(ev);
                    msg->Release();
                }
                num = impl_->sockets->ReceiveMessagesOnPollGroup(impl_->poll_group, msgs, kMaxMessages);
            }
        } else if (!impl_->is_server && impl_->client_conn != k_HSteamNetConnection_Invalid) {
            int num = impl_->sockets->ReceiveMessagesOnConnection(impl_->client_conn, msgs, kMaxMessages);
            while (num > 0) {
                for (int i = 0; i < num; ++i) {
                    SteamNetworkingMessage_t* msg = msgs[i];
                    const TransportEvent ev{
                        .type = TransportEventType::DataReceived,
                        .peer_id = kServerPeerId,
                        .payload = std::span<const std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(msg->m_pData), msg->m_cbSize),
                        .reason = DisconnectReason::None,
                    };
                    callback(ev);
                    msg->Release();
                }
                num = impl_->sockets->ReceiveMessagesOnConnection(impl_->client_conn, msgs, kMaxMessages);
            }
        }
    }

    std::vector<Impl::QueuedEvent> pending;
    {
        std::lock_guard lock(impl_->queue_mutex);
        pending.swap(impl_->event_queue);
    }

    for (const auto& ev : pending) {
        const TransportEvent public_ev{
            .type = ev.type,
            .peer_id = ev.peer_id,
            .payload = ev.payload,
            .reason = ev.reason,
        };
        callback(public_ev);
    }
}

bool GnsTransport::is_running() const {
    return impl_->is_running;
}

bool GnsTransport::is_server() const {
    return impl_->is_server;
}

float GnsTransport::rtt_ms(PeerId peer_id) const {
    if (!impl_->is_running || impl_->sockets == nullptr) {
        return -1.0f;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (impl_->is_server) {
        conn = static_cast<HSteamNetConnection>(peer_id);
    } else {
        conn = impl_->client_conn;
    }

    if (conn == k_HSteamNetConnection_Invalid) {
        return -1.0f;
    }

    SteamNetConnectionRealTimeStatus_t status{};
    if (impl_->sockets->GetConnectionRealTimeStatus(conn, &status, 0, nullptr) == k_EResultOK) {
        return static_cast<float>(status.m_nPing);
    }
    return -1.0f;
}

} // namespace engine::net
