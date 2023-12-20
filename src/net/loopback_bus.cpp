#include <engine/net/loopback_bus.h>

namespace engine::net {

LoopbackBus::~LoopbackBus() {
    const std::lock_guard<std::mutex> lock(mutex_);
    servers_.clear();
}

bool LoopbackBus::register_server(std::uint16_t port, LoopbackTransport* server) {
    if (server == nullptr || port == 0) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (servers_.contains(port)) {
        return false;
    }
    servers_.emplace(port, server);
    return true;
}

void LoopbackBus::unregister_server(std::uint16_t port, LoopbackTransport* server) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(port);
    if (it != servers_.end() && it->second == server) {
        servers_.erase(it);
    }
}

LoopbackTransport* LoopbackBus::find_server(std::uint16_t port) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(port);
    if (it != servers_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace engine::net
