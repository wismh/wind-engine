#include "gns_common.h"

#if defined(_WIN32)

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace engine::net {

namespace {

std::atomic<int> s_gns_ref_count{0};
std::mutex s_registry_mutex{};
std::unordered_map<HSteamListenSocket, GnsBaseTransport*> s_listeners{};
std::unordered_map<HSteamNetConnection, GnsBaseTransport*> s_connections{};

void global_gns_connection_status_changed_callback(SteamNetConnectionStatusChangedCallback_t* info) {
    GnsBaseTransport* target = nullptr;
    {
        std::lock_guard lock(s_registry_mutex);
        if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
            auto it = s_listeners.find(info->m_info.m_hListenSocket);
            if (it != s_listeners.end()) {
                target = it->second;
            }
        }
        if (target == nullptr) {
            auto it = s_connections.find(info->m_hConn);
            if (it != s_connections.end()) {
                target = it->second;
            }
        }
    }

    if (target != nullptr) {
        target->on_connection_status_changed(info);
    }
}

} // namespace

GnsBaseTransport::GnsBaseTransport(bool use_steam_id)
    : use_steam_id_(use_steam_id) {
    if (s_gns_ref_count.fetch_add(1) == 0) {
        SteamDatagramErrMsg err_msg{};
        if (!GameNetworkingSockets_Init(nullptr, err_msg)) {
            s_gns_ref_count.fetch_sub(1);
            return;
        }
        SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(
            global_gns_connection_status_changed_callback);
    }
    sockets_ = SteamNetworkingSockets();
}

GnsBaseTransport::~GnsBaseTransport() {
    stop();
    if (s_gns_ref_count.fetch_sub(1) == 1) {
        GameNetworkingSockets_Kill();
    }
}

bool GnsBaseTransport::setup_server_sockets(HSteamListenSocket listen_socket) {
    listen_socket_ = listen_socket;
    poll_group_ = sockets_->CreatePollGroup();
    if (poll_group_ == k_HSteamNetPollGroup_Invalid) {
        sockets_->CloseListenSocket(listen_socket_);
        listen_socket_ = k_HSteamListenSocket_Invalid;
        return false;
    }

    {
        std::lock_guard lock(s_registry_mutex);
        s_listeners[listen_socket_] = this;
    }

    is_server_ = true;
    is_running_ = true;
    return true;
}

bool GnsBaseTransport::setup_client_connection(HSteamNetConnection client_conn) {
    client_conn_ = client_conn;
    {
        std::lock_guard lock(s_registry_mutex);
        s_connections[client_conn_] = this;
    }

    is_server_ = false;
    is_running_ = true;
    return true;
}

void GnsBaseTransport::enqueue(QueuedEvent ev) {
    std::lock_guard lock(state_mutex_);
    event_queue_.push_back(std::move(ev));
}

void GnsBaseTransport::on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    if (sockets_ == nullptr) {
        return;
    }

    switch (info->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting: {
        if (is_server_) {
            if (sockets_->AcceptConnection(info->m_hConn) != k_EResultOK) {
                sockets_->CloseConnection(info->m_hConn, 0, nullptr, false);
                return;
            }
            sockets_->SetConnectionPollGroup(info->m_hConn, poll_group_);
            {
                std::lock_guard lock(s_registry_mutex);
                s_connections[info->m_hConn] = this;
            }
        }
        break;
    }
    case k_ESteamNetworkingConnectionState_Connected: {
        PeerId remote_id = static_cast<PeerId>(info->m_hConn);
        if (use_steam_id_ && info->m_info.m_identityRemote.GetSteamID64() != 0) {
            remote_id = info->m_info.m_identityRemote.GetSteamID64();
        }

        if (is_server_) {
            {
                std::lock_guard lock(state_mutex_);
                peer_to_conn_[remote_id] = info->m_hConn;
                conn_to_peer_[info->m_hConn] = remote_id;
            }
            enqueue(QueuedEvent{
                .type = TransportEventType::Connected,
                .peer_id = remote_id,
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

        PeerId remote_id = static_cast<PeerId>(info->m_hConn);
        if (use_steam_id_ && info->m_info.m_identityRemote.GetSteamID64() != 0) {
            remote_id = info->m_info.m_identityRemote.GetSteamID64();
        }

        if (is_server_) {
            {
                std::lock_guard lock(state_mutex_);
                peer_to_conn_.erase(remote_id);
                conn_to_peer_.erase(info->m_hConn);
            }
            enqueue(QueuedEvent{
                .type = TransportEventType::Disconnected,
                .peer_id = remote_id,
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
            s_connections.erase(info->m_hConn);
        }
        sockets_->CloseConnection(info->m_hConn, 0, nullptr, false);
        if (!is_server_ && info->m_hConn == client_conn_) {
            client_conn_ = k_HSteamNetConnection_Invalid;
            is_running_ = false;
        }
        break;
    }
    default:
        break;
    }
}

void GnsBaseTransport::disconnect(PeerId peer_id) {
    if (!is_running_ || sockets_ == nullptr) {
        return;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (is_server_) {
        std::lock_guard lock(state_mutex_);
        auto it = peer_to_conn_.find(peer_id);
        if (it != peer_to_conn_.end()) {
            conn = it->second;
            peer_to_conn_.erase(it);
            conn_to_peer_.erase(conn);
        } else {
            conn = static_cast<HSteamNetConnection>(peer_id);
        }
    } else {
        conn = client_conn_;
    }

    if (conn != k_HSteamNetConnection_Invalid) {
        {
            std::lock_guard lock(s_registry_mutex);
            s_connections.erase(conn);
        }
        sockets_->CloseConnection(conn, 0, nullptr, false);
        if (!is_server_) {
            client_conn_ = k_HSteamNetConnection_Invalid;
            is_running_ = false;
        }
    }
}

void GnsBaseTransport::stop() {
    if (!is_running_ || sockets_ == nullptr) {
        return;
    }

    if (is_server_) {
        if (listen_socket_ != k_HSteamListenSocket_Invalid) {
            {
                std::lock_guard lock(s_registry_mutex);
                s_listeners.erase(listen_socket_);
            }
            sockets_->CloseListenSocket(listen_socket_);
            listen_socket_ = k_HSteamListenSocket_Invalid;
        }

        if (poll_group_ != k_HSteamNetPollGroup_Invalid) {
            sockets_->DestroyPollGroup(poll_group_);
            poll_group_ = k_HSteamNetPollGroup_Invalid;
        }

        std::vector<HSteamNetConnection> conns_to_close{};
        {
            std::lock_guard lock(state_mutex_);
            for (const auto& [conn, peer] : conn_to_peer_) {
                conns_to_close.push_back(conn);
            }
            peer_to_conn_.clear();
            conn_to_peer_.clear();
            event_queue_.clear();
        }

        {
            std::lock_guard lock(s_registry_mutex);
            for (auto conn : conns_to_close) {
                s_connections.erase(conn);
            }
        }
        for (auto conn : conns_to_close) {
            sockets_->CloseConnection(conn, 0, nullptr, false);
        }
    } else {
        if (client_conn_ != k_HSteamNetConnection_Invalid) {
            {
                std::lock_guard lock(s_registry_mutex);
                s_connections.erase(client_conn_);
            }
            sockets_->CloseConnection(client_conn_, 0, nullptr, false);
            client_conn_ = k_HSteamNetConnection_Invalid;
        }
        {
            std::lock_guard lock(state_mutex_);
            event_queue_.clear();
        }
    }

    is_running_ = false;
}

bool GnsBaseTransport::send(PeerId target, std::span<const std::uint8_t> data,
    DeliveryMode mode, std::uint8_t channel) {
    (void)channel;
    if (!is_running_ || sockets_ == nullptr) {
        return false;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (is_server_) {
        std::lock_guard lock(state_mutex_);
        auto it = peer_to_conn_.find(target);
        if (it != peer_to_conn_.end()) {
            conn = it->second;
        } else {
            conn = static_cast<HSteamNetConnection>(target);
        }
    } else {
        conn = client_conn_;
    }

    if (conn == k_HSteamNetConnection_Invalid) {
        return false;
    }

    int send_flags = k_nSteamNetworkingSend_Reliable;
    switch (mode) {
    case DeliveryMode::Unreliable:
        send_flags = k_nSteamNetworkingSend_Unreliable;
        break;
    case DeliveryMode::UnreliableImmediate:
        send_flags = k_nSteamNetworkingSend_UnreliableNoNagle;
        break;
    case DeliveryMode::ReliableOrdered:
        send_flags = k_nSteamNetworkingSend_Reliable;
        break;
    }

    const EResult res = sockets_->SendMessageToConnection(
        conn, data.data(), static_cast<uint32>(data.size()), send_flags, nullptr);
    return res == k_EResultOK;
}

void GnsBaseTransport::poll_events(const INetworkTransport::EventCallback& callback) {
    if (sockets_ == nullptr) {
        return;
    }

    sockets_->RunCallbacks();

    if (is_running_) {
        constexpr int kMaxMessages = 64;
        SteamNetworkingMessage_t* msgs[kMaxMessages];

        if (is_server_ && poll_group_ != k_HSteamNetPollGroup_Invalid) {
            int num = sockets_->ReceiveMessagesOnPollGroup(poll_group_, msgs, kMaxMessages);
            while (num > 0) {
                for (int i = 0; i < num; ++i) {
                    SteamNetworkingMessage_t* msg = msgs[i];
                    PeerId sender_id = static_cast<PeerId>(msg->m_conn);
                    {
                        std::lock_guard lock(state_mutex_);
                        auto it = conn_to_peer_.find(msg->m_conn);
                        if (it != conn_to_peer_.end()) {
                            sender_id = it->second;
                        } else if (use_steam_id_ && msg->m_identityPeer.GetSteamID64() != 0) {
                            sender_id = msg->m_identityPeer.GetSteamID64();
                        }
                    }

                    const TransportEvent ev{
                        .type = TransportEventType::DataReceived,
                        .peer_id = sender_id,
                        .payload = std::span<const std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(msg->m_pData),
                            static_cast<std::size_t>(msg->m_cbSize)),
                        .reason = DisconnectReason::None,
                    };
                    callback(ev);
                    msg->Release();
                }
                num = sockets_->ReceiveMessagesOnPollGroup(poll_group_, msgs, kMaxMessages);
            }
        } else if (!is_server_ && client_conn_ != k_HSteamNetConnection_Invalid) {
            int num = sockets_->ReceiveMessagesOnConnection(client_conn_, msgs, kMaxMessages);
            while (num > 0) {
                for (int i = 0; i < num; ++i) {
                    SteamNetworkingMessage_t* msg = msgs[i];
                    const TransportEvent ev{
                        .type = TransportEventType::DataReceived,
                        .peer_id = kServerPeerId,
                        .payload = std::span<const std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(msg->m_pData),
                            static_cast<std::size_t>(msg->m_cbSize)),
                        .reason = DisconnectReason::None,
                    };
                    callback(ev);
                    msg->Release();
                }
                num = sockets_->ReceiveMessagesOnConnection(client_conn_, msgs, kMaxMessages);
            }
        }
    }

    std::vector<QueuedEvent> pending;
    {
        std::lock_guard lock(state_mutex_);
        pending.swap(event_queue_);
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

bool GnsBaseTransport::is_running() const {
    return is_running_;
}

bool GnsBaseTransport::is_server() const {
    return is_server_;
}

float GnsBaseTransport::rtt_ms(PeerId peer_id) const {
    if (!is_running_ || sockets_ == nullptr) {
        return -1.0f;
    }

    HSteamNetConnection conn = k_HSteamNetConnection_Invalid;
    if (is_server_) {
        std::lock_guard lock(state_mutex_);
        auto it = peer_to_conn_.find(peer_id);
        if (it != peer_to_conn_.end()) {
            conn = it->second;
        } else {
            conn = static_cast<HSteamNetConnection>(peer_id);
        }
    } else {
        conn = client_conn_;
    }

    if (conn == k_HSteamNetConnection_Invalid) {
        return -1.0f;
    }

    SteamNetConnectionRealTimeStatus_t status{};
    if (sockets_->GetConnectionRealTimeStatus(conn, &status, 0, nullptr) == k_EResultOK) {
        return static_cast<float>(status.m_nPing);
    }
    return -1.0f;
}

} // namespace engine::net

#endif
