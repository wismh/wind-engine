#include <engine/net/replica_writer.h>

#include <engine/net/net_entity_map.h>

namespace engine::net {

ReplicaWriter::ReplicaWriter(ByteWriter& bytes, const ecs::World& world) noexcept
    : bytes_(&bytes)
    , world_(&world) {}

ByteWriter& ReplicaWriter::bytes() noexcept {
    return *bytes_;
}

void ReplicaWriter::write_entity(ecs::Entity entity) {
    bytes_->write_u32(find_net_id(*world_, entity).value_or(kInvalidNetId));
}

} // namespace engine::net
