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
#include <optional>
#include <span>
#include <vector>

namespace engine::net {
namespace {

constexpr std::size_t kSlots = 2;

EntitySnapshot entity(NetId id, PeerId owner, std::vector<std::vector<std::uint8_t>> slots) {
    EntitySnapshot out{.id = id, .owner = owner};
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (!slots[slot].empty()) {
            out.mask |= std::uint32_t{1} << slot;
            out.components.push_back(slots[slot]);
        }
    }
    return out;
}

std::optional<SnapshotState> decode(std::span<const std::uint8_t> bytes, const SnapshotState* baseline,
        std::size_t slot_count = kSlots) {
    ByteReader reader(bytes);
    if (reader.read_u16() != kSnapshotMessageId) {
        return std::nullopt;
    }
    Tick tick = kNoTick;
    Tick baseline_tick = kNoTick;
    SnapshotPeerHeader peer;
    if (!read_snapshot_header(reader, tick, baseline_tick, peer)) {
        return std::nullopt;
    }
    return decode_snapshot(reader, tick, baseline_tick, baseline, slot_count);
}

SnapshotState base_state() {
    return SnapshotState{
            .tick = 10,
            .entities = {entity(1, kServerPeerId, {{1, 2, 3}, {7}}), entity(2, 5, {{4, 5}, {}}),
                    entity(3, kServerPeerId, {{9}, {8}}), entity(4, kServerPeerId, {{6}, {}})},
    };
}

TEST(SnapshotCodec, FullRoundTrip) {
    const SnapshotState state = base_state();
    const auto bytes = encode_snapshot(state, nullptr, SnapshotPeerHeader{});
    const auto decoded = decode(bytes, nullptr);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, state);
}

TEST(SnapshotCodec, DeltaRoundTripCarriesOnlyChanges) {
    const SnapshotState baseline = base_state();
    SnapshotState current{
            .tick = 14,
            .entities =
                    {
                            entity(1, kServerPeerId, {{1, 2, 4}, {7}}), // slot 0 changed
                            entity(2, 6, {{4, 5}, {}}),                 // owner changed
                            entity(3, kServerPeerId, {{9}, {}}),        // slot 1 removed
                            // 4 removed
                            entity(5, kServerPeerId, {{1}, {2}}), // new
                    },
    };
    const auto bytes = encode_snapshot(current, &baseline, SnapshotPeerHeader{});
    const auto full = encode_snapshot(current, nullptr, SnapshotPeerHeader{});
    EXPECT_LT(bytes.size(), full.size() + 4u);

    const auto decoded = decode(bytes, &baseline);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, current);

    // An unchanged entity is not written at all.
    SnapshotState same = baseline;
    same.tick = 12;
    const auto empty_delta = encode_snapshot(same, &baseline, SnapshotPeerHeader{});
    EXPECT_EQ(empty_delta.size(), 2u + 4u + 4u + 4u + 1u + 2u + 2u);
    const auto decoded_same = decode(empty_delta, &baseline);
    ASSERT_TRUE(decoded_same.has_value());
    EXPECT_EQ(*decoded_same, same);
}

TEST(SnapshotCodec, TruncatedAndTrailingBytesRejected) {
    const SnapshotState baseline = base_state();
    SnapshotState current = baseline;
    current.tick = 12;
    current.entities[0].components[0] = {0, 0, 0};
    const auto bytes = encode_snapshot(current, &baseline, SnapshotPeerHeader{});
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(decode(std::span(bytes).first(size), &baseline).has_value()) << size;
    }
    auto longer = bytes;
    longer.push_back(0);
    EXPECT_FALSE(decode(longer, &baseline).has_value());
}

TEST(SnapshotCodec, SlotBeyondRegistryRejected) {
    const auto bytes = encode_snapshot(base_state(), nullptr, SnapshotPeerHeader{});
    EXPECT_FALSE(decode(bytes, nullptr, 1).has_value());
}

TEST(SnapshotCodec, BaselineMustMatch) {
    const SnapshotState baseline = base_state();
    SnapshotState current = baseline;
    current.tick = 12;
    const auto delta = encode_snapshot(current, &baseline, SnapshotPeerHeader{});
    EXPECT_FALSE(decode(delta, nullptr).has_value());
    SnapshotState other = baseline;
    other.tick = 8;
    EXPECT_FALSE(decode(delta, &other).has_value());
    const auto full = encode_snapshot(current, nullptr, SnapshotPeerHeader{});
    EXPECT_FALSE(decode(full, &baseline).has_value());
}

TEST(SnapshotCodec, HeaderRejectsBaselineNotOlderThanTick) {
    ByteWriter writer;
    writer.write_u32(10);
    writer.write_u32(10);
    writer.write_u32(kNoTick);
    writer.write_i8(kNoInputSlack);
    ByteReader reader(writer.bytes());
    Tick tick = kNoTick;
    Tick baseline = kNoTick;
    SnapshotPeerHeader peer;
    EXPECT_FALSE(read_snapshot_header(reader, tick, baseline, peer));
}

TEST(SnapshotCodec, PeerHeaderRoundTrip) {
    const SnapshotPeerHeader sent{.input_tick = 77, .input_slack = -3};
    const auto bytes = encode_snapshot(base_state(), nullptr, sent);
    ByteReader reader(bytes);
    ASSERT_EQ(reader.read_u16(), kSnapshotMessageId);
    Tick tick = kNoTick;
    Tick baseline = kNoTick;
    SnapshotPeerHeader peer;
    ASSERT_TRUE(read_snapshot_header(reader, tick, baseline, peer));
    EXPECT_EQ(tick, 10u);
    EXPECT_EQ(baseline, kNoTick);
    EXPECT_EQ(peer, sent);
    EXPECT_EQ(decode_snapshot(reader, tick, baseline, nullptr, kSlots), base_state());

    // The header is cut anywhere inside the peer fields.
    for (std::size_t size = 2 + 8; size < 2 + 13; ++size) {
        ByteReader cut(std::span(bytes).first(size));
        (void)cut.read_u16();
        EXPECT_FALSE(read_snapshot_header(cut, tick, baseline, peer)) << size;
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

} // namespace
} // namespace engine::net
