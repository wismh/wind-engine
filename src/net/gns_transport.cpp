#include <engine/net/gns_transport.h>

#if defined(_WIN32)

#include "gns_common.h"

namespace engine::net {

struct GnsTransport::Impl : public GnsBaseTransport {
    Impl()
        : GnsBaseTransport(/*use_steam_id=*/false) {}

    bool start_server(std::uint16_t port) {
        if (is_running_ || sockets_ == nullptr) {
            return false;
        }

        SteamNetworkingIPAddr addr{};
        addr.Clear();
        addr.m_port = port;

        HSteamListenSocket listen = sockets_->CreateListenSocketIP(addr, 0, nullptr);
        if (listen == k_HSteamListenSocket_Invalid) {
            return false;
        }

        return setup_server_sockets(listen);
    }

    bool connect(std::string_view address, std::uint16_t port) {
        if (is_running_ || sockets_ == nullptr) {
            return false;
        }

        SteamNetworkingIPAddr addr{};
        addr.Clear();
        addr.ParseString(std::string(address).c_str());
        addr.m_port = port;

        HSteamNetConnection conn = sockets_->ConnectByIPAddress(addr, 0, nullptr);
        if (conn == k_HSteamNetConnection_Invalid) {
            return false;
        }

        return setup_client_connection(conn);
    }
};

GnsTransport::GnsTransport()
    : impl_(std::make_unique<Impl>()) {}

GnsTransport::~GnsTransport() = default;

bool GnsTransport::start_server(std::uint16_t port) {
    return impl_->start_server(port);
}

bool GnsTransport::connect(std::string_view address, std::uint16_t port) {
    return impl_->connect(address, port);
}

void GnsTransport::disconnect(PeerId peer_id) {
    impl_->disconnect(peer_id);
}

void GnsTransport::stop() {
    impl_->stop();
}

bool GnsTransport::send(PeerId target, std::span<const std::uint8_t> data,
    DeliveryMode mode, std::uint8_t channel) {
    return impl_->send(target, data, mode, channel);
}

void GnsTransport::poll_events(const EventCallback& callback) {
    impl_->poll_events(callback);
}

bool GnsTransport::is_running() const {
    return impl_->is_running();
}

bool GnsTransport::is_server() const {
    return impl_->is_server();
}

float GnsTransport::rtt_ms(PeerId peer_id) const {
    return impl_->rtt_ms(peer_id);
}

std::size_t GnsTransport::pending_send_bytes(PeerId peer_id) const {
    return impl_->pending_send_bytes(peer_id);
}

} // namespace engine::net

#else

namespace engine::net {

struct GnsTransport::Impl {};

GnsTransport::GnsTransport()
    : impl_(std::make_unique<Impl>()) {}

GnsTransport::~GnsTransport() = default;

bool GnsTransport::start_server(std::uint16_t /*port*/) {
    return false;
}

bool GnsTransport::connect(std::string_view /*address*/, std::uint16_t /*port*/) {
    return false;
}

void GnsTransport::disconnect(PeerId /*peer_id*/) {}

void GnsTransport::stop() {}

bool GnsTransport::send(PeerId /*target*/, std::span<const std::uint8_t> /*data*/,
    DeliveryMode /*mode*/, std::uint8_t /*channel*/) {
    return false;
}

void GnsTransport::poll_events(const EventCallback& /*callback*/) {}

bool GnsTransport::is_running() const {
    return false;
}

bool GnsTransport::is_server() const {
    return false;
}

float GnsTransport::rtt_ms(PeerId /*peer_id*/) const {
    return -1.0f;
}

std::size_t GnsTransport::pending_send_bytes(PeerId /*peer_id*/) const {
    return 0;
}

} // namespace engine::net

#endif
