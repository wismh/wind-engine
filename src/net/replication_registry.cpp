#include <engine/net/replication_registry.h>

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/log.h>

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cassert>

namespace engine::net {

ReplicationRegistry::ReplicationRegistry() {
    add<Transform>(
            "Transform",
            [](ReplicaWriter& writer, const Transform& value) {
                writer.bytes().write_vec3(value.position);
                writer.bytes().write_quat(value.rotation);
                writer.bytes().write_vec3(value.scale);
            },
            [](ReplicaReader& reader, Transform& value) {
                return reader.bytes().read(value.position) && reader.bytes().read(value.rotation) &&
                       reader.bytes().read(value.scale);
            },
            ReplicatedOptions<Transform>{
                    .interpolate =
                            [](const Transform& from, const Transform& to, float t) {
                                Transform out = to;
                                out.position = glm::mix(from.position, to.position, t);
                                out.rotation = glm::slerp(from.rotation, to.rotation, t);
                                out.scale = glm::mix(from.scale, to.scale, t);
                                return out;
                            },
            });

    // The parent travels as its NetId. Applied through set_parent / clear_parent so `Children` stays right.
    add<Parent>(
            "Parent", [](ReplicaWriter& writer, const Parent& value) { writer.write_entity(value.entity); },
            [](ReplicaReader& reader, Parent& value) {
                const auto parent = reader.read_entity();
                if (!parent.has_value()) {
                    return false;
                }
                value.entity = *parent;
                return true;
            },
            ReplicatedOptions<Parent>{
                    .apply = [](ecs::World& world, ecs::Entity entity,
                                     const Parent& value) { (void)ecs::set_parent(world, entity, value.entity); },
                    .remove = [](ecs::World& world, ecs::Entity entity) { ecs::clear_parent(world, entity); },
            });
}

std::size_t ReplicationRegistry::size() const noexcept {
    return entries_.size();
}

const ReplicationRegistry::Entry& ReplicationRegistry::entry(std::size_t slot) const {
    assert(slot < entries_.size());
    return entries_[slot];
}

std::uint32_t ReplicationRegistry::schema_hash() const noexcept {
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= 16777619u;
    };
    for (const Entry& entry : entries_) {
        for (const char c : entry.name) {
            mix(static_cast<std::uint8_t>(c));
        }
        mix(0);
    }
    return hash;
}

void ReplicationRegistry::add_entry(Entry entry) {
    if (entries_.size() >= kMaxReplicated) {
        log::error("net: more than 32 replicated components; '" + entry.name + "' is not replicated");
        assert(false && "too many replicated components");
        return;
    }
    const bool duplicate = std::any_of(
            entries_.begin(), entries_.end(), [&entry](const Entry& existing) { return existing.name == entry.name; });
    if (duplicate) {
        log::error("net: replicated component '" + entry.name + "' is registered twice");
        assert(false && "replicated component registered twice");
        return;
    }
    entries_.push_back(std::move(entry));
}

} // namespace engine::net
