#pragma once

#include <engine/net/network_transport.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::net {

class GnsTransport final : public INetworkTransport {
public:
    GnsTransport();
    ~GnsTransport() override;

    GnsTransport(const GnsTransport&) = delete;
    GnsTransport& operator=(const GnsTransport&) = delete;

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

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::net
