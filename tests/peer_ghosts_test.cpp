#include <gtest/gtest.h>

#include "net/peer_ghosts.h"
#include "net/snapshot_codec.h"

#include "fixtures/net_harness.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::net {
namespace {

using testing::parse_snapshot;

constexpr std::size_t kSlots = 2;
constexpr std::size_t kNoLimit = 60000;

// Holds the entities a test sends, at stable addresses.
struct Capture {
    std::vector<EntitySnapshot> entities{};

    void add(NetId id, std::vector<std::uint8_t> slot0, PeerId owner = kServerPeerId) {
        EntitySnapshot entity{.id = id, .owner = owner, .mask = 1u};
        entity.components = std::make_shared<const EntitySnapshot::Blobs>(EntitySnapshot::Blobs{std::move(slot0)});
        entities.push_back(std::move(entity));
    }

    [[nodiscard]] std::vector<RelevantEntity> relevant() const {
        std::vector<RelevantEntity> out;
        for (std::size_t i = 0; i < entities.size(); ++i) {
            out.push_back(RelevantEntity{.entity = &entities[i], .capture_index = static_cast<std::uint32_t>(i)});
        }
        return out;
    }
};

const EntityWeight kEven = [](const RelevantEntity&) { return 1.0f; };

testing::WireSnapshot send(PeerGhosts& ghosts, Tick tick, const std::vector<RelevantEntity>& relevant,
        std::size_t budget = kNoLimit, const EntityWeight& weight = kEven) {
    const auto built = ghosts.build(tick, SnapshotPeerHeader{}, relevant, budget, kSlots, weight);
    auto wire = parse_snapshot(built.bytes);
    EXPECT_TRUE(wire.has_value());
    return wire.value_or(testing::WireSnapshot{});
}

TEST(PeerGhosts, WritesUntilAckedThenNothing) {
    Capture capture;
    capture.add(1, {1});
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    EXPECT_EQ(send(ghosts, 2, relevant).ids, std::vector<NetId>{1});
    EXPECT_EQ(send(ghosts, 4, relevant).ids, std::vector<NetId>{1});
    ghosts.acknowledge(2);
    EXPECT_TRUE(send(ghosts, 6, relevant).ids.empty());
}

TEST(PeerGhosts, AckOfASnapshotNeverSentOrOutsideTheWindowIsIgnored) {
    Capture capture;
    capture.add(1, {1});
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    (void)send(ghosts, 2, relevant);
    ghosts.acknowledge(99);
    EXPECT_EQ(ghosts.acked(), kNoTick);
    EXPECT_EQ(send(ghosts, 2 + kSnapshotWindowTicks, relevant).ids, std::vector<NetId>{1});
    ghosts.acknowledge(2);
    EXPECT_EQ(ghosts.acked(), kNoTick);
}

TEST(PeerGhosts, AnyAckNewerThanTheValueConfirmsItInAnyOrder) {
    Capture capture;
    capture.add(1, {1});
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    (void)send(ghosts, 2, relevant);
    (void)send(ghosts, 4, relevant);
    ghosts.acknowledge(4);
    ghosts.acknowledge(2); // arrives after
    EXPECT_EQ(ghosts.acked(), 4u);
    EXPECT_TRUE(send(ghosts, 6, relevant).ids.empty());
}

TEST(PeerGhosts, ChangedValueIsNotConfirmedByAnOlderAck) {
    Capture first;
    first.add(1, {1});
    Capture second;
    second.add(1, {2});
    PeerGhosts ghosts;
    (void)send(ghosts, 2, first.relevant());
    (void)send(ghosts, 4, second.relevant());
    ghosts.acknowledge(2);
    EXPECT_EQ(send(ghosts, 6, second.relevant()).ids, std::vector<NetId>{1});
    ghosts.acknowledge(6);
    EXPECT_TRUE(send(ghosts, 8, second.relevant()).ids.empty());
}

TEST(PeerGhosts, LeavingIsListedUntilAcked) {
    Capture capture;
    capture.add(1, {1});
    PeerGhosts ghosts;
    (void)send(ghosts, 2, capture.relevant());
    ghosts.acknowledge(2);

    EXPECT_EQ(send(ghosts, 4, {}).removed_ids, std::vector<NetId>{1});
    EXPECT_EQ(send(ghosts, 6, {}).removed_ids, std::vector<NetId>{1});
    ghosts.acknowledge(6);
    EXPECT_TRUE(send(ghosts, 8, {}).removed_ids.empty());
    EXPECT_TRUE(ghosts.ghosts().empty());
}

TEST(PeerGhosts, NeverWrittenLeavesSilently) {
    Capture capture;
    capture.add(1, std::vector<std::uint8_t>(500, 1));
    capture.add(2, std::vector<std::uint8_t>(500, 2));
    PeerGhosts ghosts;
    const auto wire = send(ghosts, 2, capture.relevant(), 600);
    ASSERT_EQ(wire.ids.size(), 1u); // the other one was deferred, so the peer never heard of it
    const NetId written = wire.ids.front();

    const auto after = send(ghosts, 4, {});
    EXPECT_EQ(after.removed_ids, std::vector<NetId>{written});
}

TEST(PeerGhosts, ReentryBeforeTheRemovalAckStartsOver) {
    Capture capture;
    capture.add(1, {1});
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    (void)send(ghosts, 2, relevant);
    ghosts.acknowledge(2);
    (void)send(ghosts, 4, {});

    const auto wire = send(ghosts, 6, relevant);
    EXPECT_TRUE(wire.removed_ids.empty());
    EXPECT_EQ(wire.ids, std::vector<NetId>{1}); // the value did not change, but the peer may have dropped it

    ghosts.acknowledge(4); // its removal, from before the entity came back: says nothing now
    EXPECT_EQ(send(ghosts, 8, relevant).ids, std::vector<NetId>{1});
    ghosts.acknowledge(8);
    EXPECT_TRUE(send(ghosts, 10, relevant).ids.empty());
}

TEST(PeerGhosts, BudgetDefersAndEveryoneGetsATurn) {
    Capture capture;
    for (NetId id = 1; id <= 10; ++id) {
        capture.add(id, std::vector<std::uint8_t>(100, static_cast<std::uint8_t>(id)));
    }
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    std::vector<int> writes(11, 0);
    constexpr std::size_t kBudget = 400; // three records of 119 bytes
    for (Tick tick = 2; tick <= 8; tick += 2) {
        const auto built = ghosts.build(tick, SnapshotPeerHeader{}, relevant, kBudget, kSlots, kEven);
        EXPECT_LE(built.bytes.size(), kBudget);
        EXPECT_EQ(built.relevant, 10u);
        const auto wire = parse_snapshot(built.bytes);
        ASSERT_TRUE(wire.has_value());
        EXPECT_EQ(wire->ids.size(), 3u);
        EXPECT_EQ(built.deferred, 7u);
        for (const NetId id : wire->ids) {
            ++writes[id];
        }
    }
    for (NetId id = 1; id <= 10; ++id) {
        EXPECT_GE(writes[id], 1) << id;
    }
}

TEST(PeerGhosts, HeavierEntitiesGoMoreOften) {
    Capture capture;
    capture.add(1, std::vector<std::uint8_t>(100, 1));
    capture.add(2, std::vector<std::uint8_t>(100, 2));
    PeerGhosts ghosts;
    const auto relevant = capture.relevant();
    const EntityWeight weight = [](const RelevantEntity& entity) { return entity.entity->id == 1 ? 1.0f : 0.25f; };
    int heavy = 0;
    int light = 0;
    for (Tick tick = 2; tick <= 200; tick += 2) {
        const auto wire = send(ghosts, tick, relevant, 130, weight);
        ASSERT_EQ(wire.ids.size(), 1u);
        (wire.ids.front() == 1 ? heavy : light) += 1;
    }
    EXPECT_GT(light, 0);
    EXPECT_GT(heavy, light * 2);
}

TEST(PeerGhosts, MandatoryAlwaysGoesAndARecordOverTheBudgetStillGoesAlone) {
    Capture capture;
    capture.add(1, std::vector<std::uint8_t>(800, 1));
    capture.add(2, std::vector<std::uint8_t>(800, 2));
    capture.add(3, std::vector<std::uint8_t>(5000, 3));
    PeerGhosts ghosts;
    auto relevant = capture.relevant();
    relevant[0].mandatory = true;

    const EntityWeight big_first = [](const RelevantEntity& entity) { return entity.entity->id == 3 ? 1.0f : 0.1f; };
    const auto wire = send(ghosts, 2, relevant, 1200, big_first);
    // The mandatory one, and the top of the order, however large.
    EXPECT_EQ(wire.ids, (std::vector<NetId>{1, 3}));
}

TEST(PeerGhosts, NewChildGoesWithItsNeverWrittenParent) {
    Capture capture;
    capture.add(1, std::vector<std::uint8_t>(100, 1));
    capture.add(2, std::vector<std::uint8_t>(100, 2));
    capture.add(3, std::vector<std::uint8_t>(100, 3));
    PeerGhosts ghosts;
    auto relevant = capture.relevant();
    relevant[2].parent = 2;
    relevant[1].parent = 1;

    const EntityWeight child_first = [](const RelevantEntity& entity) {
        return entity.entity->id == 3 ? 1.0f : 0.1f;
    };
    EXPECT_EQ(send(ghosts, 2, relevant, 130, child_first).ids, (std::vector<NetId>{1, 2, 3}));
}

TEST(PeerGhosts, RemovalsDoNotCrowdOutRecordsAndAllGetListed) {
    Capture leaving;
    for (NetId id = 1; id <= 200; ++id) {
        leaving.add(id, {1});
    }
    PeerGhosts ghosts;
    (void)send(ghosts, 2, leaving.relevant());
    ghosts.acknowledge(2);

    Capture staying;
    for (NetId id = 1000; id <= 1009; ++id) {
        staying.add(id, std::vector<std::uint8_t>(100, 1));
    }
    std::vector<int> listed(201, 0);
    for (Tick tick = 4; tick <= 40; tick += 2) {
        const auto wire = send(ghosts, tick, staying.relevant(), 600);
        EXPECT_LE(wire.size, 600u);
        EXPECT_GE(wire.ids.size(), 3u);
        EXPECT_LT(wire.removed_ids.size(), 200u);
        for (const NetId id : wire.removed_ids) {
            ++listed[id];
        }
    }
    for (NetId id = 1; id <= 200; ++id) {
        EXPECT_GE(listed[id], 1) << id;
    }
}

} // namespace
} // namespace engine::net
