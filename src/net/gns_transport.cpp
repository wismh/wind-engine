#include <engine/net/gns_transport.h>
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

} // namespace engine::net
