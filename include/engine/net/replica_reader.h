#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>

#include <optional>

namespace engine::net {

// What a replicated component's `read` gets: the bytes, plus `NetId` references translated back to this
// machine's entities through `NetEntityMap`.
class ReplicaReader {
public:
    ReplicaReader(ByteReader& bytes, const ecs::World& world) noexcept;

    [[nodiscard]] ByteReader& bytes() noexcept;
    // nullopt for `kInvalidNetId`, for an id this machine does not know, and on truncated input
    // (then `bytes().is_ok()` is false).
    [[nodiscard]] std::optional<ecs::Entity> read_entity();

private:
    ByteReader* bytes_;
    const ecs::World* world_;
};

} // namespace engine::net
