#pragma once

#include <engine/net/network_transport.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace engine::net {

class SteamTransport final : public INetworkTransport {
public:
    SteamTransport();
    ~SteamTransport() override;

    SteamTransport(const SteamTransport&) = delete;
    SteamTransport& operator=(const SteamTransport&) = delete;

    // In Steam P2P, port is used as the virtual channel / listen slot
    bool start_server(std::uint16_t port) override;
    // Address is stringified SteamID64 (e.g. "76561198000000000")
    bool connect(std::string_view address, std::uint16_t port) override;
    bool connect_steam_id(std::uint64_t steam_id64, std::uint16_t port = 0);

    void disconnect(PeerId peer_id) override;
    void stop() override;

    bool send(PeerId target, std::span<const std::uint8_t> data,
        DeliveryMode mode = DeliveryMode::ReliableOrdered, std::uint8_t channel = 0) override;

    void poll_events(const EventCallback& callback) override;

    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] bool is_server() const override;
    [[nodiscard]] float rtt_ms(PeerId peer_id) const override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::net
