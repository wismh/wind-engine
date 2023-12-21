#pragma once

#include <engine/net/network_identity.h>

#include <cstdint>

namespace engine::net {

class NetIdGenerator {
public:
    explicit constexpr NetIdGenerator(NetId start_id = 1) noexcept
        : next_id_(start_id == 0 ? 1 : start_id) {}

    constexpr NetId next() noexcept {
        const NetId id = next_id_++;
        if (next_id_ == 0) {
            next_id_ = 1;
        }
        return id;
    }

    constexpr void reset(NetId start_id = 1) noexcept {
        next_id_ = start_id == 0 ? 1 : start_id;
    }

    [[nodiscard]] constexpr NetId peek_next_value() const noexcept {
        return next_id_;
    }

private:
    NetId next_id_{1};
};

} // namespace engine::net
