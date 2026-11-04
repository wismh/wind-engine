#include <gtest/gtest.h>

#include "net/snapshot_codec.h"
#include "net/snapshot_state.h"

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_messages.h>
#include <engine/net/replication_registry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace engine::net {
namespace {

constexpr std::size_t kSlots = 2;

EntitySnapshot entity(NetId id, PeerId owner, const std::vector<std::vector<std::uint8_t>>& slots) {
    EntitySnapshot out{.id = id, .owner = owner};
    EntitySnapshot::Blobs blobs;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (!slots[slot].empty()) {
            out.mask |= std::uint32_t{1} << slot;
            blobs.push_back(slots[slot]);
        }
    }
    out.components = std::make_shared<const EntitySnapshot::Blobs>(std::move(blobs));
    return out;
}

std::optional<SnapshotDelta> decode(std::span<const std::uint8_t> bytes, std::size_t slot_count = kSlots) {
    ByteReader reader(bytes);
    if (reader.read_u16() != kSnapshotMessageId) {
        return std::nullopt;
    }
    Tick tick = kNoTick;
    SnapshotPeerHeader peer;
    if (!read_snapshot_header(reader, tick, peer)) {
        return std::nullopt;
    }
    return decode_snapshot(reader, tick, slot_count);
}

SnapshotState base_state() {
    return SnapshotState{
            .tick = 10,
            .entities = {entity(1, kServerPeerId, {{1, 2, 3}, {7}}), entity(2, 5, {{4, 5}, {}}),
                    entity(3, kServerPeerId, {{9}, {8}}), entity(4, kServerPeerId, {{6}, {}})},
    };
}

// Every entity whole, owner included.
std::vector<std::uint8_t> encode_whole(const SnapshotState& state, const SnapshotPeerHeader& peer = {}) {
    std::vector<SnapshotWrite> writes;
    for (const EntitySnapshot& record : state.entities) {
        writes.push_back(SnapshotWrite{.entity = &record, .write_owner = true, .changed = record.mask});
    }
    return encode_snapshot(state.tick, peer, {}, writes);
}

TEST(SnapshotCodec, WholeRoundTrip) {
    const SnapshotState state = base_state();
    const auto delta = decode(encode_whole(state));
    ASSERT_TRUE(delta.has_value());
    const auto applied = apply_snapshot(nullptr, *delta, /*strict=*/true);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(*applied, state);
}

TEST(SnapshotCodec, RecordsLayerOntoThePreviousState) {
    const SnapshotState previous = base_state();
    const EntitySnapshot changed_slot = entity(1, kServerPeerId, {{1, 2, 4}, {7}});
    const EntitySnapshot changed_owner = entity(2, 6, {{4, 5}, {}});
    const EntitySnapshot lost_slot = entity(3, kServerPeerId, {{9}, {}});
    const EntitySnapshot fresh = entity(5, kServerPeerId, {{1}, {2}});
    const std::vector<SnapshotWrite> writes{
            {.entity = &changed_slot, .changed = 0b01u},
            {.entity = &changed_owner, .write_owner = true},
            {.entity = &lost_slot, .lost = 0b10u},
            {.entity = &fresh, .write_owner = true, .changed = 0b11u},
    };
    const std::vector<NetId> removed{4};
    const auto bytes = encode_snapshot(14, SnapshotPeerHeader{}, removed, writes);

    std::size_t expected = kSnapshotHeaderBytes + removed.size() * 4u;
    for (const SnapshotWrite& write : writes) {
        expected += snapshot_write_size(write);
    }
    EXPECT_EQ(bytes.size(), expected);

    const auto delta = decode(bytes);
    ASSERT_TRUE(delta.has_value());
    const auto applied = apply_snapshot(&previous, *delta, /*strict=*/true);
    ASSERT_TRUE(applied.has_value());
    const SnapshotState wanted{
            .tick = 14,
            .entities = {changed_slot, changed_owner, lost_slot, fresh},
    };
    EXPECT_EQ(*applied, wanted);

    // A snapshot that writes nothing leaves every entity as it was.
    const auto nothing = decode(encode_snapshot(16, SnapshotPeerHeader{}, {}, {}));
    ASSERT_TRUE(nothing.has_value());
    const auto same = apply_snapshot(&*applied, *nothing, /*strict=*/true);
    ASSERT_TRUE(same.has_value());
    EXPECT_EQ(same->entities, applied->entities);
    EXPECT_EQ(encode_snapshot(16, SnapshotPeerHeader{}, {}, {}).size(), kSnapshotHeaderBytes);
}

TEST(SnapshotCodec, TruncatedAndTrailingBytesRejected) {
    const auto bytes = encode_whole(base_state());
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(decode(std::span(bytes).first(size)).has_value()) << size;
    }
    auto longer = bytes;
    longer.push_back(0);
    EXPECT_FALSE(decode(longer).has_value());
}

TEST(SnapshotCodec, SlotBeyondRegistryRejected) {
    EXPECT_FALSE(decode(encode_whole(base_state()), 1).has_value());
}

TEST(SnapshotCodec, SlotBothChangedAndLostRejected) {
    const EntitySnapshot record = entity(1, kServerPeerId, {{1}, {}});
    const std::vector<SnapshotWrite> writes{{.entity = &record, .write_owner = true, .changed = 0b01u, .lost = 0b01u}};
    EXPECT_FALSE(decode(encode_snapshot(4, SnapshotPeerHeader{}, {}, writes)).has_value());
}

TEST(SnapshotCodec, IdBothRemovedAndWrittenRejected) {
    const EntitySnapshot record = entity(1, kServerPeerId, {{1}, {}});
    const std::vector<SnapshotWrite> writes{{.entity = &record, .write_owner = true, .changed = 0b01u}};
    const std::vector<NetId> removed{1};
    EXPECT_FALSE(decode(encode_snapshot(4, SnapshotPeerHeader{}, removed, writes)).has_value());
}

TEST(SnapshotCodec, UnknownEntityWithoutOwnerDependsOnStrict) {
    const EntitySnapshot record = entity(9, kServerPeerId, {{1}, {}});
    const std::vector<SnapshotWrite> writes{{.entity = &record, .changed = 0b01u}};
    const auto delta = decode(encode_snapshot(4, SnapshotPeerHeader{}, {}, writes));
    ASSERT_TRUE(delta.has_value());
    EXPECT_FALSE(apply_snapshot(nullptr, *delta, /*strict=*/true).has_value());
    const auto lenient = apply_snapshot(nullptr, *delta, /*strict=*/false);
    ASSERT_TRUE(lenient.has_value());
    EXPECT_TRUE(lenient->entities.empty());
}

TEST(SnapshotCodec, RemovedIdNotHeldIsIgnored) {
    const SnapshotState previous = base_state();
    const std::vector<NetId> removed{77};
    const auto delta = decode(encode_snapshot(12, SnapshotPeerHeader{}, removed, {}));
    ASSERT_TRUE(delta.has_value());
    const auto applied = apply_snapshot(&previous, *delta, /*strict=*/true);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(applied->entities, previous.entities);
}

TEST(SnapshotCodec, HeaderRejectsNoTick) {
    ByteWriter writer;
    writer.write_u32(kNoTick);
    writer.write_u32(kNoTick);
    writer.write_i8(kNoInputSlack);
    ByteReader reader(writer.bytes());
    Tick tick = kNoTick;
    SnapshotPeerHeader peer;
    EXPECT_FALSE(read_snapshot_header(reader, tick, peer));
}

TEST(SnapshotCodec, PeerHeaderRoundTrip) {
    const SnapshotPeerHeader sent{.input_tick = 77, .input_slack = -3};
    const SnapshotState state = base_state();
    const auto bytes = encode_whole(state, sent);
    ByteReader reader(bytes);
    ASSERT_EQ(reader.read_u16(), kSnapshotMessageId);
    Tick tick = kNoTick;
    SnapshotPeerHeader peer;
    ASSERT_TRUE(read_snapshot_header(reader, tick, peer));
    EXPECT_EQ(tick, 10u);
    EXPECT_EQ(peer, sent);
    const auto delta = decode_snapshot(reader, tick, kSlots);
    ASSERT_TRUE(delta.has_value());
    EXPECT_EQ(apply_snapshot(nullptr, *delta, /*strict=*/true), state);

    // The header is cut anywhere inside the peer fields.
    for (std::size_t size = 2 + 4; size < 2 + 9; ++size) {
        ByteReader cut(std::span(bytes).first(size));
        (void)cut.read_u16();
        EXPECT_FALSE(read_snapshot_header(cut, tick, peer)) << size;
    }
}

TEST(SnapshotCodec, CaptureUsesRegistrySlotsAndNetIds) {
    ecs::World world;
    const auto& registry = world.ctx<ReplicationRegistry>();
    ASSERT_EQ(registry.size(), 2u);
    EXPECT_EQ(registry.entry(0).name, "Transform");
    EXPECT_EQ(registry.entry(1).name, "Parent");

    const ecs::Entity parent = world.create();
    const ecs::Entity child = world.create();
    const ecs::Entity bare = world.create();
    world.emplace<Transform>(parent, Transform{.position = {1.0f, 2.0f, 3.0f}});
    world.emplace<Transform>(child, Transform{});
    ASSERT_TRUE(bind_net_entity(world, child, 9));
    ASSERT_TRUE(bind_net_entity(world, parent, 4));
    ASSERT_TRUE(bind_net_entity(world, bare, 6, 3));
    ASSERT_TRUE(ecs::set_parent(world, child, parent));

    const SnapshotState state = capture_snapshot(world, registry, 7);
    ASSERT_EQ(state.entities.size(), 3u);
    EXPECT_EQ(state.entities[0].id, 4u);
    EXPECT_EQ(state.entities[1].id, 6u);
    EXPECT_EQ(state.entities[2].id, 9u);
    EXPECT_EQ(state.entities[0].mask, 0b01u);
    EXPECT_EQ(state.entities[0].component(0).size(), 40u);
    EXPECT_EQ(state.entities[1].mask, 0u);
    EXPECT_EQ(state.entities[1].owner, 3u);
    EXPECT_EQ(state.entities[2].mask, 0b11u);

    // Parent is written as the parent's NetId.
    ByteReader parent_bytes(state.entities[2].component(1));
    EXPECT_EQ(parent_bytes.read_u32(), 4u);
}

TEST(SnapshotCodec, CaptureSharesBlobsOfUnchangedEntities) {
    ecs::World world;
    const auto& registry = world.ctx<ReplicationRegistry>();
    const ecs::Entity still = world.create();
    const ecs::Entity moving = world.create();
    world.emplace<Transform>(still, Transform{});
    world.emplace<Transform>(moving, Transform{});
    ASSERT_TRUE(bind_net_entity(world, still, 1));
    ASSERT_TRUE(bind_net_entity(world, moving, 2));

    const SnapshotState first = capture_snapshot(world, registry, 2);
    world.get<Transform>(moving).position.x = 1.0f;
    const SnapshotState second = capture_snapshot(world, registry, 4, &first);
    EXPECT_EQ(second.entities[0].components, first.entities[0].components);
    EXPECT_NE(second.entities[1].components, first.entities[1].components);
}

TEST(SnapshotCodec, SchemaHashFollowsRegistration) {
    ecs::World a;
    ecs::World b;
    EXPECT_EQ(a.ctx<ReplicationRegistry>().schema_hash(), b.ctx<ReplicationRegistry>().schema_hash());
    struct Extra {
        int value{0};
    };
    register_replicated<Extra>(
            b, "Extra", [](ReplicaWriter& writer, const Extra& value) { writer.bytes().write_i32(value.value); },
            [](ReplicaReader& reader, Extra& value) { return reader.bytes().read(value.value); });
    EXPECT_EQ(b.ctx<ReplicationRegistry>().size(), 3u);
    EXPECT_NE(a.ctx<ReplicationRegistry>().schema_hash(), b.ctx<ReplicationRegistry>().schema_hash());
}

TEST(SnapshotCodec, CaptureKeepsTheLastValueOfAComponentTooLargeToSend) {
    struct Big {
        std::vector<std::uint8_t> bytes;
    };
    ecs::World world;
    register_replicated<Big>(
            world, "Big", [](ReplicaWriter& out, const Big& big) { out.bytes().append(big.bytes); },
            [](ReplicaReader&, Big&) { return true; });
    const ReplicationRegistry& registry = world.ctx<ReplicationRegistry>();
    // After the slots every registry starts with.
    const std::size_t slot = registry.size() - 1;
    const ecs::Entity entity = world.create();
    world.emplace<NetIdentity>(entity, NetIdentity{.id = 5});

    // Never fit: nothing to keep, so the slot is not there.
    world.emplace<Big>(entity, Big{.bytes = std::vector<std::uint8_t>(kMaxSnapshotBlobBytes + 1, 1)});
    EXPECT_EQ(capture_snapshot(world, registry, 1).find(5)->mask, 0u);

    world.get<Big>(entity).bytes.assign(kMaxSnapshotBlobBytes, 2);
    const SnapshotState fits = capture_snapshot(world, registry, 2);
    ASSERT_TRUE(fits.find(5)->has(slot));
    EXPECT_EQ(fits.find(5)->component(slot).size(), kMaxSnapshotBlobBytes);

    world.get<Big>(entity).bytes.assign(70000, 3);
    const SnapshotState kept = capture_snapshot(world, registry, 3, &fits);
    ASSERT_TRUE(kept.find(5)->has(slot));
    EXPECT_EQ(kept.find(5)->component(slot).size(), kMaxSnapshotBlobBytes);
    EXPECT_EQ(kept.find(5)->component(slot).front(), 2u);
}

} // namespace
} // namespace engine::net
