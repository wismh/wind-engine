#include <engine/net/loopback_transport.h>

namespace engine::net {

LoopbackTransport::LoopbackTransport(LoopbackBus& bus)
    : bus_(bus) {}

LoopbackTransport::~LoopbackTransport() {
    stop();
}

bool LoopbackTransport::start_server(std::uint16_t port) {
    std::lock_guard lock(state_mutex_);
    if (is_running_) {
        return false;
    }
    if (!bus_.register_server(port, this)) {
        return false;
    }
    port_ = port;
    is_server_ = true;
    is_running_ = true;
    return true;
}

bool LoopbackTransport::connect(std::string_view address, std::uint16_t port) {
    (void)address;
    {
        std::lock_guard lock(state_mutex_);
        if (is_running_) {
            return false;
        }
        is_server_ = false;
        is_running_ = true;
        port_ = port;
    }

    LoopbackTransport* server = bus_.find_server(port);
    if (server == nullptr) {
        enqueue_event(InternalEvent{
            .type = TransportEventType::Disconnected,
            .peer_id = kServerPeerId,
            .payload = {},
            .reason = DisconnectReason::ConnectionFailed,
        });
        std::lock_guard lock(state_mutex_);
        is_running_ = false;
        return false;
    }

    PeerId assigned_id = kInvalidPeerId;
    if (!server->accept_loopback_client(this, assigned_id)) {
        enqueue_event(InternalEvent{
            .type = TransportEventType::Disconnected,
            .peer_id = kServerPeerId,
            .payload = {},
            .reason = DisconnectReason::ConnectionFailed,
        });
        std::lock_guard lock(state_mutex_);
        is_running_ = false;
        return false;
    }

    {
        std::lock_guard lock(state_mutex_);
        connections_.emplace(kServerPeerId, PeerConnection{
            .remote_peer_id = assigned_id,
            .endpoint = server,
        });
    }

    enqueue_event(InternalEvent{
        .type = TransportEventType::Connected,
        .peer_id = kServerPeerId,
        .payload = {},
        .reason = DisconnectReason::None,
    });
    return true;
}

bool LoopbackTransport::accept_loopback_client(LoopbackTransport* client, PeerId& out_client_id) {
    if (client == nullptr) {
        return false;
    }
    std::lock_guard lock(state_mutex_);
    if (!is_running_ || !is_server_) {
        return false;
    }
    out_client_id = next_peer_id_++;
    connections_.emplace(out_client_id, PeerConnection{
        .remote_peer_id = kServerPeerId,
        .endpoint = client,
    });

    enqueue_event(InternalEvent{
        .type = TransportEventType::Connected,
        .peer_id = out_client_id,
        .payload = {},
        .reason = DisconnectReason::None,
    });
    return true;
}

void LoopbackTransport::disconnect(PeerId peer_id) {
    LoopbackTransport* target_endpoint = nullptr;
    PeerId remote_peer_id = kInvalidPeerId;
    {
        std::lock_guard lock(state_mutex_);
        auto it = connections_.find(peer_id);
        if (it == connections_.end()) {
            return;
        }
        target_endpoint = it->second.endpoint;
        remote_peer_id = it->second.remote_peer_id;
        connections_.erase(it);
    }

    if (target_endpoint != nullptr) {
        target_endpoint->notify_peer_disconnected(remote_peer_id, DisconnectReason::ClosedByPeer);
    }

    enqueue_event(InternalEvent{
        .type = TransportEventType::Disconnected,
        .peer_id = peer_id,
        .payload = {},
        .reason = DisconnectReason::ClosedByPeer,
    });
}

void LoopbackTransport::stop() {
    std::unordered_map<PeerId, PeerConnection> conns;
    bool was_server = false;
    std::uint16_t port = 0;
    {
        std::lock_guard lock(state_mutex_);
        if (!is_running_) {
            return;
        }
        is_running_ = false;
        was_server = is_server_;
        port = port_;
        conns.swap(connections_);
    }

    if (was_server) {
        bus_.unregister_server(port, this);
    }

    for (const auto& [peer_id, conn] : conns) {
        if (conn.endpoint != nullptr) {
            conn.endpoint->notify_peer_disconnected(conn.remote_peer_id, DisconnectReason::ClosedByPeer);
        }
        enqueue_event(InternalEvent{
            .type = TransportEventType::Disconnected,
            .peer_id = peer_id,
            .payload = {},
            .reason = DisconnectReason::ClosedByPeer,
        });
    }
}

bool LoopbackTransport::send(PeerId target, std::span<const std::uint8_t> data,
    DeliveryMode mode, std::uint8_t channel) {
    (void)mode;
    (void)channel;
    LoopbackTransport* target_endpoint = nullptr;
    PeerId sender_id = kInvalidPeerId;
    {
        std::lock_guard lock(state_mutex_);
        if (!is_running_) {
            return false;
        }
        auto it = connections_.find(target);
        if (it == connections_.end()) {
            return false;
        }
        target_endpoint = it->second.endpoint;
        sender_id = it->second.remote_peer_id;
    }

    if (target_endpoint != nullptr) {
        target_endpoint->receive_incoming_data(sender_id, data);
        return true;
    }
    return false;
}

void LoopbackTransport::poll_events(const EventCallback& callback) {
    std::vector<InternalEvent> events_to_process;
    {
        std::lock_guard lock(queue_mutex_);
        events_to_process.swap(event_queue_);
    }

    for (const auto& ev : events_to_process) {
        const TransportEvent public_event{
            .type = ev.type,
            .peer_id = ev.peer_id,
            .payload = ev.payload,
            .reason = ev.reason,
        };
        callback(public_event);
    }
}

bool LoopbackTransport::is_running() const {
    std::lock_guard lock(state_mutex_);
    return is_running_;
}

bool LoopbackTransport::is_server() const {
    std::lock_guard lock(state_mutex_);
    return is_server_;
}

float LoopbackTransport::rtt_ms(PeerId peer_id) const {
    std::lock_guard lock(state_mutex_);
    if (connections_.contains(peer_id)) {
        return 0.0f;
    }
    return -1.0f;
}

void LoopbackTransport::receive_incoming_data(PeerId sender_id, std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> buffer(data.begin(), data.end());
    enqueue_event(InternalEvent{
        .type = TransportEventType::DataReceived,
        .peer_id = sender_id,
        .payload = std::move(buffer),
        .reason = DisconnectReason::None,
    });
}

void LoopbackTransport::notify_peer_disconnected(PeerId peer_id, DisconnectReason reason) {
    {
        std::lock_guard lock(state_mutex_);
        connections_.erase(peer_id);
    }
    enqueue_event(InternalEvent{
        .type = TransportEventType::Disconnected,
        .peer_id = peer_id,
        .payload = {},
        .reason = reason,
    });
}

void LoopbackTransport::enqueue_event(InternalEvent event) {
    std::lock_guard lock(queue_mutex_);
    event_queue_.push_back(std::move(event));
}

} // namespace engine::net
