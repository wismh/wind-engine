#include <gtest/gtest.h>

#include "fixtures/net_harness.h"

#include <engine/core/compression.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_client.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_payload_delivered_event.h>
#include <engine/net/net_server.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace engine::net {
namespace {

using testing::NetClientSide;
using testing::NetHarness;

constexpr MessageId kChunk = 0x2000;
constexpr MessageId kOtherChunk = 0x2001;

std::vector<std::uint8_t> pattern(std::size_t size, std::uint8_t seed = 1) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(seed + i * 31u + (i >> 8));
    }
    return bytes;
}

// What a client's handler saw, in order.
struct Received {
    std::vector<std::vector<std::uint8_t>> payloads{};
    std::vector<MessageId> ids{};

    void listen(MessageDispatcher& dispatcher, MessageId id) {
        dispatcher.register_handler(id, [this, id](PeerId from, ByteReader& reader) {
            EXPECT_EQ(from, kServerPeerId);
            const auto bytes = reader.read_bytes(reader.remaining());
            ASSERT_TRUE(bytes.has_value());
            payloads.emplace_back(bytes->begin(), bytes->end());
            ids.push_back(id);
        });
    }
};

// The delivered events of the server, collected per step.
struct Delivered {
    ecs::EventCursor<NetPayloadDeliveredEvent> cursor{};
    std::vector<NetPayloadDeliveredEvent> events{};

    void collect(ecs::World& server) {
        for (const auto& event : ecs::EventReader<NetPayloadDeliveredEvent>(server, cursor)) {
            events.push_back(event);
        }
    }
};

NetServer& server_of(NetHarness& net) {
    return net.server.ctx<NetServer>();
}

PeerId connected_peer(NetHarness& net, std::size_t index = 0) {
    net.step(2);
    return server_of(net).peers().at(index);
}

// Steps until `done` or `frames` run out, collecting events.
template<typename Done>
bool step_until(NetHarness& net, Delivered& delivered, int frames, Done done) {
    for (int i = 0; i < frames; ++i) {
        net.step();
        delivered.collect(net.server);
        if (done()) {
            return true;
        }
    }
    return false;
}

TEST(NetPayload, SmallPayloadArrivesThroughTheDispatcherAndIsAcknowledged) {
    NetHarness net(9501);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);

    const auto bytes = pattern(100);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, bytes);
    ASSERT_NE(id, kNoPayload);
    EXPECT_EQ(server_of(net).queued_payload_bytes(peer), 100u);
    EXPECT_EQ(server_of(net).payload_progress(id)->total_bytes, 100u);

    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 10, [&] { return !delivered.events.empty(); }));
    ASSERT_EQ(received.payloads.size(), 1u);
    EXPECT_EQ(received.payloads[0], bytes);
    ASSERT_EQ(delivered.events.size(), 1u);
    EXPECT_EQ(delivered.events[0].id, id);
    EXPECT_EQ(delivered.events[0].peer, peer);
    EXPECT_FALSE(server_of(net).payload_progress(id).has_value());
    EXPECT_EQ(server_of(net).queued_payload_bytes(peer), 0u);
}

TEST(NetPayload, LargePayloadIsFragmentedAndReassembled) {
    NetServerConfig config;
    config.peer_bytes_per_second = 8 * 1024 * 1024;
    config.transport_backlog_bytes = 1024 * 1024;
    NetHarness net(9502, config);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);

    // Larger than GameNetworkingSockets' 512 KiB message limit.
    const auto bytes = pattern(1024 * 1024);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, bytes);
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 60, [&] { return !delivered.events.empty(); }));
    ASSERT_EQ(received.payloads.size(), 1u);
    EXPECT_EQ(received.payloads[0], bytes);
    EXPECT_EQ(delivered.events.at(0).id, id);

    std::size_t begins = 0;
    std::size_t fragments = 0;
    for (const auto& sent : net.server_link.sent()) {
        const auto message = testing::message_id(sent.bytes);
        if (message == kPayloadBeginMessageId || message == kPayloadFragmentMessageId) {
            EXPECT_LE(sent.bytes.size(), kPayloadFragmentBytes + 13u);
            EXPECT_EQ(sent.mode, DeliveryMode::ReliableOrdered);
            (message == kPayloadBeginMessageId ? begins : fragments) += 1;
        }
    }
    EXPECT_EQ(begins, 1u);
    EXPECT_EQ(fragments + begins, bytes.size() / kPayloadFragmentBytes);
}

TEST(NetPayload, EmptyPayloadArrives) {
    NetHarness net(9503);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    (void)server_of(net).send_payload(peer, kChunk, std::span<const std::uint8_t>{});
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 10, [&] { return !delivered.events.empty(); }));
    ASSERT_EQ(received.payloads.size(), 1u);
    EXPECT_TRUE(received.payloads[0].empty());
}

TEST(NetPayload, HigherPriorityGoesFirst) {
    NetServerConfig config;
    config.payload_aging_per_second = 0.0f;
    NetHarness net(9504, config);
    Received received;
    auto& dispatcher = net.client.ctx<NetClient>().dispatcher();
    received.listen(dispatcher, kChunk);
    received.listen(dispatcher, kOtherChunk);
    net.connect();
    const PeerId peer = connected_peer(net);

    const auto low = pattern(5000, 1);
    const auto high = pattern(5000, 2);
    (void)server_of(net).send_payload(peer, kChunk, low, {.priority = 1});
    (void)server_of(net).send_payload(peer, kOtherChunk, high, {.priority = 5});
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 120, [&] { return delivered.events.size() == 2; }));
    ASSERT_EQ(received.ids.size(), 2u);
    EXPECT_EQ(received.ids[0], kOtherChunk);
    EXPECT_EQ(received.ids[1], kChunk);
    EXPECT_EQ(received.payloads[0], high);
    EXPECT_EQ(received.payloads[1], low);
}

TEST(NetPayload, SetPriorityPreemptsAtAFragmentBoundary) {
    NetServerConfig config;
    config.payload_aging_per_second = 0.0f;
    NetHarness net(9505, config);
    Received received;
    auto& dispatcher = net.client.ctx<NetClient>().dispatcher();
    received.listen(dispatcher, kChunk);
    received.listen(dispatcher, kOtherChunk);
    net.connect();
    const PeerId peer = connected_peer(net);

    const auto first = pattern(100 * 1024, 1);
    const auto second = pattern(20 * 1024, 2);
    const PayloadId first_id = server_of(net).send_payload(peer, kChunk, first, {.priority = 2});
    const PayloadId second_id = server_of(net).send_payload(peer, kOtherChunk, second, {.priority = 1});
    net.step(10);
    ASSERT_GT(server_of(net).payload_progress(first_id)->sent_bytes, 0u);
    ASSERT_LT(server_of(net).payload_progress(first_id)->sent_bytes, first.size());
    EXPECT_EQ(server_of(net).payload_progress(second_id)->sent_bytes, 0u);

    EXPECT_TRUE(server_of(net).set_payload_priority(second_id, 3));
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 400, [&] { return delivered.events.size() == 2; }));
    EXPECT_EQ(delivered.events[0].id, second_id);
    EXPECT_EQ(delivered.events[1].id, first_id);
    EXPECT_EQ(received.payloads[0], second);
    EXPECT_EQ(received.payloads[1], first);
    EXPECT_FALSE(server_of(net).set_payload_priority(first_id, 9));
}

TEST(NetPayload, CancelBeforeTheFirstFragmentSendsNothing) {
    NetHarness net(9506);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, pattern(100));
    EXPECT_TRUE(server_of(net).cancel_payload(id));
    EXPECT_FALSE(server_of(net).cancel_payload(id));
    Delivered delivered;
    EXPECT_FALSE(step_until(net, delivered, 10, [&] { return !delivered.events.empty(); }));
    EXPECT_TRUE(received.payloads.empty());
    for (const auto& sent : net.server_link.sent()) {
        const auto message = testing::message_id(sent.bytes);
        EXPECT_NE(message, kPayloadBeginMessageId);
        EXPECT_NE(message, kPayloadCancelMessageId);
    }
}

TEST(NetPayload, CancelMidwayTellsTheClientAndNothingArrives) {
    NetHarness net(9507);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, pattern(200 * 1024));
    net.step(5);
    ASSERT_GT(server_of(net).payload_progress(id)->sent_bytes, 0u);
    EXPECT_TRUE(server_of(net).cancel_payload(id));
    const auto after_cancel = net.server_link.sent().size();
    ASSERT_EQ(testing::message_id(net.server_link.sent().back().bytes), kPayloadCancelMessageId);

    Delivered delivered;
    EXPECT_FALSE(step_until(net, delivered, 20, [&] { return !delivered.events.empty(); }));
    EXPECT_TRUE(received.payloads.empty());
    EXPECT_FALSE(server_of(net).payload_progress(id).has_value());
    EXPECT_TRUE(net.client.ctx<NetClient>().welcomed());
    for (std::size_t i = after_cancel; i < net.server_link.sent().size(); ++i) {
        EXPECT_NE(testing::message_id(net.server_link.sent()[i].bytes), kPayloadFragmentMessageId);
    }
}

TEST(NetPayload, CancelAfterTheLastByteIsQuiet) {
    NetHarness net(9508);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.server_link.set_latency_ms(100.0);
    net.connect();
    net.step(20);
    const PeerId peer = server_of(net).peers().at(0);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, pattern(100));
    net.step(1);
    ASSERT_TRUE(server_of(net).payload_progress(id)->awaiting_ack);
    EXPECT_TRUE(server_of(net).cancel_payload(id));

    Delivered delivered;
    EXPECT_FALSE(step_until(net, delivered, 30, [&] { return !delivered.events.empty(); }));
    // The client had it coming already; its ack finds nothing.
    EXPECT_EQ(received.payloads.size(), 1u);
    EXPECT_TRUE(net.client.ctx<NetClient>().welcomed());
}

TEST(NetPayload, SnapshotsGoFirstAndEveryDueTickHasOne) {
    NetServerConfig config;
    config.peer_bytes_per_second = 60 * 1024;
    NetHarness net(9509, config);
    const NetId mover = net.spawn();
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [mover](ecs::World& world) {
        if (const auto entity = find_entity(world, mover)) {
            world.get<Transform>(*entity).position.x = static_cast<float>(world.ctx<NetServer>().tick());
        }
    });
    net.connect();
    net.step(20);
    const PeerId peer = server_of(net).peers().at(0);
    (void)server_of(net).send_payload(peer, kChunk, pattern(1024 * 1024), {.priority = 1000});

    for (int frame = 0; frame < 120; ++frame) {
        const std::size_t before = net.server_link.sent().size();
        net.step();
        std::vector<std::uint16_t> messages;
        for (std::size_t i = before; i < net.server_link.sent().size(); ++i) {
            const auto message = testing::message_id(net.server_link.sent()[i].bytes);
            messages.push_back(message.value_or(0));
        }
        if (net.tick() % 2 == 0) {
            ASSERT_FALSE(messages.empty()) << "tick " << net.tick();
            EXPECT_EQ(messages.front(), kSnapshotMessageId) << "tick " << net.tick();
        } else {
            EXPECT_EQ(std::count(messages.begin(), messages.end(), kSnapshotMessageId), 0);
        }
        // Movement is not disturbed: still exactly on the render timeline.
        EXPECT_NEAR(net.client.get<Transform>(*net.client_entity(mover)).position.x,
                static_cast<double>(net.tick()) - 5.0, 1e-3)
                << "tick " << net.tick();
    }
    EXPECT_GT(server_of(net).peer_stats(peer)->payload_bytes_sent, 0u);
}

TEST(NetPayload, RateIsRespectedOverAnyWindow) {
    NetServerConfig config;
    config.peer_bytes_per_second = 30 * 1024;
    NetHarness net(9510, config);
    for (int i = 0; i < 100; ++i) {
        net.spawn();
    }
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [](ecs::World& world) {
        const float x = static_cast<float>(world.ctx<NetServer>().tick());
        for (const ecs::Entity entity : world.view<NetIdentity, Transform>()) {
            world.get<Transform>(entity).position.x = x;
        }
    });
    net.connect();
    net.step(2);
    const PeerId peer = server_of(net).peers().at(0);
    (void)server_of(net).send_payload(peer, kChunk, pattern(1024 * 1024));

    const std::size_t start = net.server_link.sent().size();
    net.step(300); // 5 seconds
    std::size_t total = 0;
    std::size_t snapshots = 0;
    std::size_t payload = 0;
    for (std::size_t i = start; i < net.server_link.sent().size(); ++i) {
        const auto& sent = net.server_link.sent()[i];
        total += sent.bytes.size();
        (testing::message_id(sent.bytes) == kSnapshotMessageId ? snapshots : payload) += sent.bytes.size();
    }
    const std::size_t allowed = 5u * config.peer_bytes_per_second;
    EXPECT_LE(total, allowed + config.peer_bytes_per_second / 10u);
    EXPECT_GT(total, allowed * 9u / 10u);
    EXPECT_GE(payload, allowed / 10u - 2048u);
    EXPECT_GT(snapshots, 0u);
}

TEST(NetPayload, PayloadIsNotStarvedByAnOversizedSnapshotSet) {
    NetServerConfig config;
    config.peer_bytes_per_second = 30 * 1024; // the snapshots alone could take it all
    NetHarness net(9511, config);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    for (int i = 0; i < 300; ++i) {
        net.spawn();
    }
    net.server.add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [](ecs::World& world) {
        const float x = static_cast<float>(world.ctx<NetServer>().tick());
        for (const ecs::Entity entity : world.view<NetIdentity, Transform>()) {
            world.get<Transform>(entity).position.x = x;
        }
    });
    net.connect();
    net.step(2);
    const PeerId peer = server_of(net).peers().at(0);
    const auto bytes = pattern(8 * 1024);
    (void)server_of(net).send_payload(peer, kChunk, bytes);

    // A tenth of 30 KiB/s is 3 KiB/s: 8 KiB within about 3 seconds.
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 240, [&] { return !delivered.events.empty(); }));
    ASSERT_EQ(received.payloads.size(), 1u);
    EXPECT_EQ(received.payloads[0], bytes);
    EXPECT_GT(server_of(net).peer_stats(peer)->deferred, 0u);
}

TEST(NetPayload, LowPriorityAgesPastAStreamOfHighPriority) {
    NetServerConfig config;
    config.peer_bytes_per_second = 30 * 1024;
    config.payload_aging_per_second = 10.0f;
    NetHarness net(9512, config);
    net.connect();
    const PeerId peer = connected_peer(net);

    const PayloadId low = server_of(net).send_payload(peer, kChunk, pattern(1000), {.priority = 0});
    Delivered delivered;
    int frames = 0;
    bool low_done = false;
    // New high-priority payloads keep coming, faster than the budget drains them.
    while (!low_done && frames < 600) {
        (void)server_of(net).send_payload(peer, kOtherChunk, pattern(1000), {.priority = 20});
        net.step();
        delivered.collect(net.server);
        low_done = std::any_of(delivered.events.begin(), delivered.events.end(),
                [low](const NetPayloadDeliveredEvent& event) { return event.id == low; });
        ++frames;
    }
    EXPECT_TRUE(low_done);
    // Priority 20 at 10 a second: about two seconds behind the newest.
    EXPECT_GT(frames, 100);
    EXPECT_LT(frames, 300);
}

TEST(NetPayload, TransportBacklogPausesPayloadsNotSnapshots) {
    NetHarness net(9513);
    net.connect();
    const PeerId peer = connected_peer(net);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, pattern(200 * 1024));
    net.server_link.set_pending_send_bytes(server_of(net).config().transport_backlog_bytes);
    const std::size_t snapshots_before = testing::snapshots_sent(net.server_link).size();
    net.step(10);
    EXPECT_EQ(server_of(net).payload_progress(id)->sent_bytes, 0u);
    EXPECT_GE(testing::snapshots_sent(net.server_link).size(), snapshots_before + 5u);

    net.server_link.set_pending_send_bytes(server_of(net).config().transport_backlog_bytes - 1u);
    net.step(5);
    EXPECT_GT(server_of(net).payload_progress(id)->sent_bytes, 0u);
}

TEST(NetPayload, RefusedSendIsRetriedAndNothingIsLost) {
    NetHarness net(9514);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const auto bytes = pattern(100 * 1024);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, bytes);
    net.step(2);
    const std::size_t before = server_of(net).payload_progress(id)->sent_bytes;
    EXPECT_GT(before, 0u);

    net.server_link.set_refuse_reliable(true);
    net.step(10);
    EXPECT_EQ(server_of(net).payload_progress(id)->sent_bytes, before);

    net.server_link.set_refuse_reliable(false);
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 120, [&] { return !delivered.events.empty(); }));
    EXPECT_EQ(received.payloads.at(0), bytes);
}

TEST(NetPayload, CompressedBytesAreDecompressedBeforeTheHandler) {
    NetHarness net(9515);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const std::vector<std::uint8_t> raw(50 * 1024, 42);
    const auto packed = compress(raw);
    ASSERT_LT(packed.size(), raw.size() / 10u);
    (void)server_of(net).send_payload(peer, kChunk, packed, {.compressed = true});
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 20, [&] { return !delivered.events.empty(); }));
    EXPECT_EQ(received.payloads.at(0), raw);
}

TEST(NetPayload, DecompressionBombDisconnects) {
    NetClientConfig client_config;
    client_config.max_payload_bytes = 1024;
    NetHarness net(9516, {}, client_config);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const auto packed = compress(std::vector<std::uint8_t>(1024 * 1024, 7));
    ASSERT_LT(packed.size(), 1024u * 8u);
    (void)server_of(net).send_payload(peer, kChunk, packed, {.compressed = true});
    net.step(20);
    EXPECT_TRUE(received.payloads.empty());
    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed());
}

TEST(NetPayload, PeerDisconnectMidPayloadClearsBothSides) {
    NetHarness net(9517);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    const PayloadId id = server_of(net).send_payload(peer, kChunk, pattern(200 * 1024));
    net.step(5);
    ASSERT_GT(server_of(net).payload_progress(id)->sent_bytes, 0u);

    net.client_link.disconnect(kServerPeerId);
    Delivered delivered;
    const std::size_t sent_before = net.server_link.sent().size();
    net.step(5);
    delivered.collect(net.server);
    EXPECT_TRUE(server_of(net).peers().empty());
    EXPECT_FALSE(server_of(net).payload_progress(id).has_value());
    EXPECT_FALSE(server_of(net).cancel_payload(id));
    EXPECT_FALSE(server_of(net).set_payload_priority(id, 1));
    EXPECT_EQ(server_of(net).queued_payload_bytes(peer), 0u);
    EXPECT_FALSE(server_of(net).peer_stats(peer).has_value());
    EXPECT_TRUE(delivered.events.empty());
    EXPECT_TRUE(received.payloads.empty());
    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed());
    // At most what was sent in the frame the server had not heard yet.
    EXPECT_LE(net.server_link.sent().size(), sent_before + 8u);
}

TEST(NetPayload, ReservedIdAndUnknownPeerAreRefused) {
    NetHarness net(9518);
    net.connect();
    const PeerId peer = connected_peer(net);
    const auto bytes = pattern(10);
    EXPECT_EQ(server_of(net).send_payload(peer, kSnapshotMessageId, bytes), kNoPayload);
    EXPECT_EQ(server_of(net).send_payload(peer, kEngineMessageLast, bytes), kNoPayload);
    EXPECT_EQ(server_of(net).send_payload(peer, kInvalidMessageId, bytes), kNoPayload);
    EXPECT_EQ(server_of(net).send_payload(peer + 100, kChunk, bytes), kNoPayload);
    EXPECT_EQ(server_of(net).send_payload(peer, kChunk, std::shared_ptr<const std::vector<std::uint8_t>>{}), kNoPayload);
    EXPECT_NE(server_of(net).send_payload(peer, kEngineMessageLast + 1u, bytes), kNoPayload);
}

TEST(NetPayload, SharedBytesGoToSeveralPeers) {
    NetHarness net(9519);
    NetClientSide& other = net.add_client();
    Received first;
    Received second;
    first.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    second.listen(other.world.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    net.step(2);
    const auto peers = server_of(net).peers();
    ASSERT_EQ(peers.size(), 2u);

    const auto shared = std::make_shared<const std::vector<std::uint8_t>>(pattern(5000));
    const PayloadId a = server_of(net).send_payload(peers[0], kChunk, shared);
    const PayloadId b = server_of(net).send_payload(peers[1], kChunk, shared);
    EXPECT_NE(a, b);
    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 60, [&] { return delivered.events.size() == 2; }));
    EXPECT_EQ(first.payloads.at(0), *shared);
    EXPECT_EQ(second.payloads.at(0), *shared);
}

TEST(NetPayload, ForgedAckIsIgnored) {
    NetHarness net(9520);
    NetClientSide& other = net.add_client();
    Received first;
    first.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.server_link.set_latency_ms(100.0);
    net.connect();
    net.step(20);
    const auto peers = server_of(net).peers();
    ASSERT_EQ(peers.size(), 2u);
    const PayloadId id = server_of(net).send_payload(peers[0], kChunk, pattern(100));
    net.step(1);
    ASSERT_TRUE(server_of(net).payload_progress(id)->awaiting_ack);

    // Another peer claims it; so does nobody, for an id that never was.
    ByteWriter forged;
    forged.write_u16(kPayloadAckMessageId);
    forged.write_u32(id);
    ASSERT_TRUE(other.link.send(kServerPeerId, forged.bytes()));
    ByteWriter unknown;
    unknown.write_u16(kPayloadAckMessageId);
    unknown.write_u32(id + 1000);
    ASSERT_TRUE(net.client_link.send(kServerPeerId, unknown.bytes()));
    net.step(1);
    EXPECT_TRUE(server_of(net).payload_progress(id).has_value());

    Delivered delivered;
    ASSERT_TRUE(step_until(net, delivered, 20, [&] { return !delivered.events.empty(); }));
    EXPECT_EQ(delivered.events.size(), 1u);
    EXPECT_EQ(delivered.events[0].peer, peers[0]);
}

// What a server would put on the wire, wrong in one way each.
std::vector<std::uint8_t> begin_message(PayloadId id, MessageId message, std::uint8_t flags, std::uint32_t total,
        std::span<const std::uint8_t> data = {}) {
    ByteWriter out;
    out.write_u16(kPayloadBeginMessageId);
    out.write_u32(id);
    out.write_u16(message);
    out.write_u8(flags);
    out.write_u32(total);
    out.append(data);
    return out.buffer();
}

std::vector<std::uint8_t> fragment_message(PayloadId id, std::uint32_t offset, std::span<const std::uint8_t> data) {
    ByteWriter out;
    out.write_u16(kPayloadFragmentMessageId);
    out.write_u32(id);
    out.write_u32(offset);
    out.append(data);
    return out.buffer();
}

struct MalformedCase {
    const char* name;
    std::vector<std::vector<std::uint8_t>> messages;
};

class NetPayloadMalformed : public ::testing::TestWithParam<MalformedCase> {};

TEST_P(NetPayloadMalformed, DisconnectsWithoutRunningTheHandler) {
    NetHarness net(9521);
    Received received;
    received.listen(net.client.ctx<NetClient>().dispatcher(), kChunk);
    net.connect();
    const PeerId peer = connected_peer(net);
    ASSERT_TRUE(net.client.ctx<NetClient>().welcomed());

    for (const auto& message : GetParam().messages) {
        ASSERT_TRUE(net.server_link.send(peer, message));
    }
    // Whatever follows in the same poll must not reach the handler either.
    const std::vector<std::uint8_t> whole{1};
    ASSERT_TRUE(net.server_link.send(peer, begin_message(77, kChunk, 0, 1, whole)));
    net.step(3);
    EXPECT_TRUE(received.payloads.empty()) << GetParam().name;
    EXPECT_FALSE(net.client.ctx<NetClient>().welcomed()) << GetParam().name;
    EXPECT_TRUE(server_of(net).peers().empty()) << GetParam().name;
}

const std::vector<std::uint8_t> kTwoBytes{1, 2};

INSTANTIATE_TEST_SUITE_P(All, NetPayloadMalformed,
        ::testing::Values(
                MalformedCase{"truncated begin", {std::vector<std::uint8_t>{0x04, 0x01, 1, 0}}},
                MalformedCase{"truncated fragment", {std::vector<std::uint8_t>{0x05, 0x01, 1, 0}}},
                MalformedCase{"truncated cancel", {std::vector<std::uint8_t>{0x06, 0x01, 1}}},
                MalformedCase{"no id", {begin_message(kNoPayload, kChunk, 0, 2)}},
                MalformedCase{"engine message id", {begin_message(1, kSnapshotMessageId, 0, 2)}},
                MalformedCase{"unknown flags", {begin_message(1, kChunk, 0x40, 2)}},
                MalformedCase{"size over the limit", {begin_message(1, kChunk, 0, 0xFFFFFFFFu)}},
                MalformedCase{"fragment of nothing", {fragment_message(5, 0, kTwoBytes)}},
                MalformedCase{"gap", {begin_message(1, kChunk, 0, 8, kTwoBytes), fragment_message(1, 3, kTwoBytes)}},
                MalformedCase{
                        "repeat", {begin_message(1, kChunk, 0, 8, kTwoBytes), fragment_message(1, 0, kTwoBytes)}},
                MalformedCase{"past the end",
                        {begin_message(1, kChunk, 0, 3, kTwoBytes), fragment_message(1, 2, kTwoBytes)}},
                MalformedCase{"begin larger than its size", {begin_message(1, kChunk, 0, 1, kTwoBytes)}},
                MalformedCase{"duplicate begin", {begin_message(1, kChunk, 0, 8), begin_message(1, kChunk, 0, 8)}},
                MalformedCase{"bad compressed bytes", {begin_message(1, kChunk, 1, 2, kTwoBytes)}}),
        [](const ::testing::TestParamInfo<MalformedCase>& info) {
            std::string name = info.param.name;
            std::replace(name.begin(), name.end(), ' ', '_');
            return name;
        });

} // namespace
} // namespace engine::net
