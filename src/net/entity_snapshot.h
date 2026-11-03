#pragma once

// docs/tech/modules/Net.md

#include <engine/net/network_identity.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::net {

// One replicated entity in a snapshot: which slots it has (`mask`, bit = registry slot) and each slot's
// encoded value, in ascending slot order.
struct EntitySnapshot {
    NetId id{kInvalidNetId};
    PeerId owner{kServerPeerId};
    std::uint32_t mask{0};
    std::vector<std::vector<std::uint8_t>> components{};

    [[nodiscard]] bool has(std::size_t slot) const noexcept {
        return (mask & (std::uint32_t{1} << slot)) != 0;
    }

    // The slot's bytes. Only for a slot `has` reports.
    [[nodiscard]] std::span<const std::uint8_t> component(std::size_t slot) const noexcept {
        const std::uint32_t below = mask & ((std::uint32_t{1} << slot) - 1u);
        return components[static_cast<std::size_t>(std::popcount(below))];
    }

    bool operator==(const EntitySnapshot&) const = default;
};

} // namespace engine::net
