#pragma once

// docs/tech/modules/Net.md

#include <engine/net/network_identity.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine::net {

// One replicated entity in a snapshot: which slots it has (`mask`, bit = registry slot) and each slot's
// encoded value, in ascending slot order. The values are shared and immutable, so copying a snapshot is a pointer
// copy per entity and every peer reads the blobs the server serialized once.
struct EntitySnapshot {
    using Blobs = std::vector<std::vector<std::uint8_t>>;

    NetId id{kInvalidNetId};
    PeerId owner{kServerPeerId};
    std::uint32_t mask{0};
    std::shared_ptr<const Blobs> components{};

    [[nodiscard]] bool has(std::size_t slot) const noexcept {
        return (mask & (std::uint32_t{1} << slot)) != 0;
    }

    // The slot's bytes. Only for a slot `has` reports.
    [[nodiscard]] std::span<const std::uint8_t> component(std::size_t slot) const noexcept {
        const std::uint32_t below = mask & ((std::uint32_t{1} << slot) - 1u);
        return (*components)[static_cast<std::size_t>(std::popcount(below))];
    }

    // Whether both hold the same values in the same slots. Owner and id are not compared.
    [[nodiscard]] bool same_values(const EntitySnapshot& other) const noexcept {
        if (mask != other.mask) {
            return false;
        }
        if (mask == 0 || components == other.components) {
            return true;
        }
        return *components == *other.components;
    }

    bool operator==(const EntitySnapshot& other) const noexcept {
        return id == other.id && owner == other.owner && same_values(other);
    }
};

} // namespace engine::net
