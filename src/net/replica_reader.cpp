#include <engine/net/replica_reader.h>

#include <engine/net/net_entity_map.h>

namespace engine::net {

ReplicaReader::ReplicaReader(ByteReader& bytes, const ecs::World& world) noexcept
    : bytes_(&bytes)
    , world_(&world) {}

ByteReader& ReplicaReader::bytes() noexcept {
    return *bytes_;
}

std::optional<ecs::Entity> ReplicaReader::read_entity() {
    const auto net_id = bytes_->read_u32();
    if (!net_id.has_value() || *net_id == kInvalidNetId) {
        return std::nullopt;
    }
    return find_entity(*world_, *net_id);
}

} // namespace engine::net
