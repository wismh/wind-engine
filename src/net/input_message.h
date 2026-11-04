#pragma once

// docs/tech/modules/Net.md

#include <engine/net/byte_stream.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_identity.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace engine::net {

// The Input message: per controlled entity, its encoded inputs from `newest` back, newest first (input i is for
// tick `newest - i`). Decoded spans point into the payload they were read from.
struct InputMessage {
    struct Entity {
        NetId id{kInvalidNetId};
        std::vector<std::span<const std::uint8_t>> inputs{};
    };

    Tick newest{kNoTick};
    std::vector<Entity> entities{};

    // The whole message, starting with its `MessageId`.
    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    // Reads the rest of a message after its `MessageId`. Nullopt on truncation, trailing bytes, `newest` of
    // `kNoTick`, no entity, more than `kMaxInputEntities` entities, an entity with no input or more than
    // `kMaxInputsPerMessage`, more inputs than ticks before `newest`, or an input over `kMaxInputBytes`.
    [[nodiscard]] static std::optional<InputMessage> decode(ByteReader& reader);
};

} // namespace engine::net
