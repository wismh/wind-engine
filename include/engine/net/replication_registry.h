#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/replica_reader.h>
#include <engine/net/replica_writer.h>
#include <engine/net/replicated_options.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::net {

// The components a snapshot carries, in slot order. Lives in `world.ctx<ReplicationRegistry>()`. Constructed
// with `Transform` (slot 0) and `Parent` (slot 1); a game adds its own with `register_replicated`. The server
// and every client must register the same list in the same order: Welcome carries `schema_hash`, and a client
// whose hash differs disconnects.
class ReplicationRegistry {
public:
    static constexpr std::size_t kMaxReplicated = 32;

    struct Entry {
        std::string name;
        // Appends the entity's value. False, writing nothing, when the entity does not have the component.
        std::function<bool(const ecs::World&, ecs::Entity, ByteWriter&)> write;
        // Decodes one value and puts it on the entity. False on bad bytes or an unresolved entity reference.
        std::function<bool(ecs::World&, ecs::Entity, std::span<const std::uint8_t>)> apply;
        // Decodes two values and puts their interpolation at `t` on the entity. Empty when the value steps.
        std::function<bool(ecs::World&, ecs::Entity, std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                float)>
                apply_interpolated;
        std::function<void(ecs::World&, ecs::Entity)> remove;
    };

    ReplicationRegistry();

    template<typename T>
    void add(std::string_view name, std::function<void(ReplicaWriter&, const T&)> write,
            std::function<bool(ReplicaReader&, T&)> read, ReplicatedOptions<T> options = {});

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const Entry& entry(std::size_t slot) const;
    // FNV-1a over the names in slot order.
    [[nodiscard]] std::uint32_t schema_hash() const noexcept;

private:
    void add_entry(Entry entry);

    std::vector<Entry> entries_{};
};

template<typename T>
void ReplicationRegistry::add(std::string_view name, std::function<void(ReplicaWriter&, const T&)> write,
        std::function<bool(ReplicaReader&, T&)> read, ReplicatedOptions<T> options) {
    // Decodes one value from a blob. Starts from T{} so a value never depends on what the entity held before.
    const auto decode = [read](const ecs::World& world, std::span<const std::uint8_t> blob, T& out) {
        ByteReader bytes(blob);
        ReplicaReader reader(bytes, world);
        out = T{};
        return read(reader, out) && bytes.is_ok();
    };
    const auto put = [apply = options.apply](ecs::World& world, ecs::Entity entity, const T& value) {
        if (apply) {
            apply(world, entity, value);
        } else if (T* existing = world.try_get<T>(entity)) {
            *existing = value;
        } else {
            world.emplace<T>(entity, value);
        }
    };

    Entry entry;
    entry.name = std::string(name);
    entry.write = [write = std::move(write)](const ecs::World& world, ecs::Entity entity, ByteWriter& bytes) {
        const T* value = world.try_get<T>(entity);
        if (value == nullptr) {
            return false;
        }
        ReplicaWriter writer(bytes, world);
        write(writer, *value);
        return true;
    };
    entry.apply = [decode, put](ecs::World& world, ecs::Entity entity, std::span<const std::uint8_t> blob) {
        T value{};
        if (!decode(world, blob, value)) {
            return false;
        }
        put(world, entity, value);
        return true;
    };
    if (options.interpolate) {
        entry.apply_interpolated = [decode, put, interpolate = std::move(options.interpolate)](ecs::World& world,
                                           ecs::Entity entity, std::span<const std::uint8_t> from_blob,
                                           std::span<const std::uint8_t> to_blob, float t) {
            T from{};
            T to{};
            if (!decode(world, from_blob, from) || !decode(world, to_blob, to)) {
                return false;
            }
            put(world, entity, interpolate(from, to, t));
            return true;
        };
    }
    if (options.remove) {
        entry.remove = std::move(options.remove);
    } else {
        entry.remove = [](ecs::World& world, ecs::Entity entity) { world.remove<T>(entity); };
    }
    add_entry(std::move(entry));
}

// Adds `T` to `world.ctx<ReplicationRegistry>()` as the next slot.
template<typename T>
void register_replicated(ecs::World& world, std::string_view name,
        std::function<void(ReplicaWriter&, const T&)> write, std::function<bool(ReplicaReader&, T&)> read,
        ReplicatedOptions<T> options = {}) {
    world.ctx<ReplicationRegistry>().add<T>(name, std::move(write), std::move(read), std::move(options));
}

} // namespace engine::net
