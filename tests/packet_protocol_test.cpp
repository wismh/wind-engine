#include <gtest/gtest.h>

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/packet_dispatcher.h>
#include <engine/net/packet_types.h>

namespace engine::net {

TEST(PacketProtocol, HeaderRoundTrip) {
    ByteWriter writer;
    write_packet_header(writer, PacketType::SpawnEntity);

    ByteReader reader(writer.span());
    const auto header = read_packet_header(reader);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->type, PacketType::SpawnEntity);
}

TEST(PacketProtocol, PacketsSerializationRoundTrip) {
    // 1. SpawnEntityPacket
    {
        const SpawnEntityPacket orig{
            .net_id = 101,
            .position = glm::vec3{10.0f, 20.0f, 30.0f},
            .rotation = glm::quat{0.7071f, 0.0f, 0.7071f, 0.0f},
            .scale = glm::vec3{2.0f, 2.0f, 2.0f},
            .parent_net_id = 50,
        };

        ByteWriter writer;
        orig.serialize(writer);

        ByteReader reader(writer.span());
        const auto res = SpawnEntityPacket::deserialize(reader);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, orig);
    }

    // 2. DespawnEntityPacket
    {
        const DespawnEntityPacket orig{.net_id = 777};

        ByteWriter writer;
        orig.serialize(writer);

        ByteReader reader(writer.span());
        const auto res = DespawnEntityPacket::deserialize(reader);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, orig);
    }

    // 3. SyncTransformPacket
    {
        const SyncTransformPacket orig{
            .net_id = 42,
            .position = glm::vec3{-5.5f, 15.2f, 0.0f},
            .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
            .scale = glm::vec3{0.5f, 0.5f, 0.5f},
        };

        ByteWriter writer;
        orig.serialize(writer);

        ByteReader reader(writer.span());
        const auto res = SyncTransformPacket::deserialize(reader);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, orig);
    }

    // 4. SyncParentPacket
    {
        const SyncParentPacket orig{
            .child_net_id = 10,
            .parent_net_id = 20,
        };

        ByteWriter writer;
        orig.serialize(writer);

        ByteReader reader(writer.span());
        const auto res = SyncParentPacket::deserialize(reader);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, orig);
    }

    // 5. HandshakePacket
    {
        const HandshakePacket orig{
            .protocol_version = 42,
            .client_name = "Player_One",
        };

        ByteWriter writer;
        orig.serialize(writer);

        ByteReader reader(writer.span());
        const auto res = HandshakePacket::deserialize(reader);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, orig);
    }
}

TEST(PacketProtocol, DispatcherRoutingAndCorruptedHandling) {
    PacketDispatcher dispatcher;

    bool raw_called = false;
    PeerId caller_peer = 0;
    dispatcher.register_handler(
        PacketType::Ping, [&](PeerId from_peer, ByteReader& reader) {
            raw_called = true;
            caller_peer = from_peer;
            EXPECT_EQ(reader.read_u32(), 12345u);
        });

    EXPECT_TRUE(dispatcher.has_handler(PacketType::Ping));
    EXPECT_FALSE(dispatcher.has_handler(PacketType::Pong));
    EXPECT_EQ(dispatcher.handler_count(), 1u);

    // Send Ping packet
    ByteWriter writer;
    write_packet_header(writer, PacketType::Ping);
    writer.write_u32(12345);

    EXPECT_TRUE(dispatcher.dispatch(100, writer.span()));
    EXPECT_TRUE(raw_called);
    EXPECT_EQ(caller_peer, 100u);

    // Dispatch unknown type fails
    ByteWriter unk_writer;
    write_packet_header(unk_writer, PacketType::Pong);
    EXPECT_FALSE(dispatcher.dispatch(100, unk_writer.span()));

    // Truncated / empty payload fails
    const std::vector<std::uint8_t> empty_buf;
    EXPECT_FALSE(dispatcher.dispatch(100, empty_buf));

    // Unregister
    dispatcher.unregister_handler(PacketType::Ping);
    EXPECT_FALSE(dispatcher.has_handler(PacketType::Ping));
    EXPECT_EQ(dispatcher.handler_count(), 0u);
}

TEST(PacketProtocol, TypedPacketHandler) {
    PacketDispatcher dispatcher;

    std::optional<HandshakePacket> received_pkt;
    dispatcher.register_packet_handler<HandshakePacket>(
        PacketType::Handshake, [&](PeerId /*from_peer*/, const HandshakePacket& pkt) {
            received_pkt = pkt;
        });

    ByteWriter writer;
    write_packet_header(writer, PacketType::Handshake);
    const HandshakePacket send_pkt{
        .protocol_version = 2,
        .client_name = "Alice",
    };
    send_pkt.serialize(writer);

    EXPECT_TRUE(dispatcher.dispatch(5, writer.span()));
    ASSERT_TRUE(received_pkt.has_value());
    EXPECT_EQ(*received_pkt, send_pkt);
}

TEST(PacketProtocol, EcsWorldReplicationIntegration) {
    ecs::World world;
    PacketDispatcher dispatcher;
    register_replication_handlers(world, dispatcher);

    const NetId entity_net = 500;

    // 1. Dispatch SpawnEntityPacket
    {
        ByteWriter writer;
        write_packet_header(writer, PacketType::SpawnEntity);
        const SpawnEntityPacket spawn{
            .net_id = entity_net,
            .position = glm::vec3{1.0f, 2.0f, 3.0f},
            .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
            .scale = glm::vec3{1.0f},
            .parent_net_id = kInvalidNetId,
        };
        spawn.serialize(writer);

        EXPECT_TRUE(dispatcher.dispatch(1, writer.span()));

        const auto local_e = find_entity(world, entity_net);
        ASSERT_TRUE(local_e.has_value());
        EXPECT_TRUE(world.valid(*local_e));

        const auto* t = world.try_get<Transform>(*local_e);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->position, glm::vec3(1.0f, 2.0f, 3.0f));
    }

    // 2. Dispatch SyncTransformPacket
    {
        ByteWriter writer;
        write_packet_header(writer, PacketType::SyncTransform);
        const SyncTransformPacket sync{
            .net_id = entity_net,
            .position = glm::vec3{10.0f, 20.0f, 30.0f},
            .rotation = glm::quat{0.0f, 1.0f, 0.0f, 0.0f},
            .scale = glm::vec3{2.0f},
        };
        sync.serialize(writer);

        EXPECT_TRUE(dispatcher.dispatch(1, writer.span()));

        const auto local_e = find_entity(world, entity_net);
        ASSERT_TRUE(local_e.has_value());
        const auto* t = world.try_get<Transform>(*local_e);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(t->scale, glm::vec3(2.0f));
    }

    // 3. Dispatch Spawn parent and SyncParentPacket
    const NetId parent_net = 600;
    {
        ByteWriter writer;
        write_packet_header(writer, PacketType::SpawnEntity);
        const SpawnEntityPacket spawn_parent{
            .net_id = parent_net,
            .position = glm::vec3{0.0f},
            .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
            .scale = glm::vec3{1.0f},
            .parent_net_id = kInvalidNetId,
        };
        spawn_parent.serialize(writer);
        EXPECT_TRUE(dispatcher.dispatch(1, writer.span()));

        // Now link child to parent
        ByteWriter link_writer;
        write_packet_header(link_writer, PacketType::SyncParent);
        const SyncParentPacket link{
            .child_net_id = entity_net,
            .parent_net_id = parent_net,
        };
        link.serialize(link_writer);
        EXPECT_TRUE(dispatcher.dispatch(1, link_writer.span()));

        const auto child_e = find_entity(world, entity_net);
        const auto parent_e = find_entity(world, parent_net);
        ASSERT_TRUE(child_e.has_value());
        ASSERT_TRUE(parent_e.has_value());

        const auto* parent_comp = world.try_get<Parent>(*child_e);
        ASSERT_NE(parent_comp, nullptr);
        EXPECT_EQ(parent_comp->entity, *parent_e);
    }

    // 4. Dispatch DespawnEntityPacket
    {
        ByteWriter writer;
        write_packet_header(writer, PacketType::DespawnEntity);
        const DespawnEntityPacket despawn{.net_id = entity_net};
        despawn.serialize(writer);

        EXPECT_TRUE(dispatcher.dispatch(1, writer.span()));

        EXPECT_EQ(find_entity(world, entity_net), std::nullopt);
    }
}

} // namespace engine::net
