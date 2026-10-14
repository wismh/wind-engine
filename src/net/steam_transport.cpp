#include <engine/net/steam_transport.h>
#include "gns_common.h"

#include <charconv>

namespace engine::net {

struct SteamTransport::Impl : public GnsBaseTransport {
    Impl()
        : GnsBaseTransport(/*use_steam_id=*/true) {}

    bool start_server(std::uint16_t port) {
        if (is_running_ || sockets_ == nullptr) {
            return false;
        }

        HSteamListenSocket listen = sockets_->CreateListenSocketP2P(port, 0, nullptr);
        if (listen == k_HSteamListenSocket_Invalid) {
            return false;
        }

        return setup_server_sockets(listen);
    }

    bool connect(std::string_view address, std::uint16_t port) {
        std::uint64_t steam_id64 = 0;
        auto [ptr, ec] = std::from_chars(address.data(), address.data() + address.size(), steam_id64);
        if (ec != std::errc{} || steam_id64 == 0) {
            return false;
        }

        return connect_steam_id(steam_id64, port);
    }

    bool connect_steam_id(std::uint64_t steam_id64, std::uint16_t port) {
        if (is_running_ || sockets_ == nullptr || steam_id64 == 0) {
            return false;
        }

        SteamNetworkingIdentity identity{};
        identity.Clear();
        identity.SetSteamID64(steam_id64);

        HSteamNetConnection conn = sockets_->ConnectP2P(identity, port, 0, nullptr);
        if (conn == k_HSteamNetConnection_Invalid) {
            return false;
        }

        return setup_client_connection(conn);
    }
};

SteamTransport::SteamTransport()
    : impl_(std::make_unique<Impl>()) {}

SteamTransport::~SteamTransport() = default;

bool SteamTransport::start_server(std::uint16_t port) {
    return impl_->start_server(port);
}

bool SteamTransport::connect(std::string_view address, std::uint16_t port) {
    return impl_->connect(address, port);
}

bool SteamTransport::connect_steam_id(std::uint64_t steam_id64, std::uint16_t port) {
    return impl_->connect_steam_id(steam_id64, port);
}

void SteamTransport::disconnect(PeerId peer_id) {
    impl_->disconnect(peer_id);
}

void SteamTransport::stop() {
    impl_->stop();
}

bool SteamTransport::send(PeerId target, std::span<const std::uint8_t> data,
    DeliveryMode mode, std::uint8_t channel) {
    return impl_->send(target, data, mode, channel);
}

void SteamTransport::poll_events(const EventCallback& callback) {
    impl_->poll_events(callback);
}

bool SteamTransport::is_running() const {
    return impl_->is_running();
}

bool SteamTransport::is_server() const {
    return impl_->is_server();
}

float SteamTransport::rtt_ms(PeerId peer_id) const {
    return impl_->rtt_ms(peer_id);
}

} // namespace engine::net
