#pragma once

#include <engine/net/network_transport.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace engine::net {

class LoopbackTransport;

class LoopbackBus {
public:
    LoopbackBus() = default;
    ~LoopbackBus();

    LoopbackBus(const LoopbackBus&) = delete;
    LoopbackBus& operator=(const LoopbackBus&) = delete;

    bool register_server(std::uint16_t port, LoopbackTransport* server);
    void unregister_server(std::uint16_t port, LoopbackTransport* server);
    LoopbackTransport* find_server(std::uint16_t port);

private:
    std::mutex mutex_{};
    std::unordered_map<std::uint16_t, LoopbackTransport*> servers_{};
};

} // namespace engine::net
