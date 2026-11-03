#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>

namespace engine::net {

// What a replicated component's `write` gets: the bytes, plus entity references translated to `NetId`.
class ReplicaWriter {
public:
    ReplicaWriter(ByteWriter& bytes, const ecs::World& world) noexcept;

    [[nodiscard]] ByteWriter& bytes() noexcept;
    // The entity's `NetId`, or `kInvalidNetId` when it is dead or not replicated.
    void write_entity(ecs::Entity entity);

private:
    ByteWriter* bytes_;
    const ecs::World* world_;
};

} // namespace engine::net
