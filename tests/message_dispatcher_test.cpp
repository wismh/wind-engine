#include <gtest/gtest.h>

#include <engine/net/loopback_bus.h>
#include <engine/net/loopback_transport.h>
#include <engine/net/message_dispatcher.h>

namespace engine::net {

TEST(MessageDispatcher, DispatchRegisteredHandler) {
    MessageDispatcher dispatcher;
    constexpr MessageId kChatMsgId = 10;

    bool handler_called = false;
    PeerId caller_peer = kInvalidPeerId;
    std::string received_text;

    dispatcher.register_handler(kChatMsgId, [&](PeerId from, ByteReader& reader) {
        handler_called = true;
        caller_peer = from;
        const auto text = reader.read_string();
        if (text) {
            received_text = *text;
        }
    });

    ByteWriter writer;
    writer.write_u16(kChatMsgId);
    writer.write_string("Hello from client!");

    EXPECT_TRUE(dispatcher.dispatch(42, writer.bytes()));
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(caller_peer, 42);
    EXPECT_EQ(received_text, "Hello from client!");

    // Unknown message ID returns false
    ByteWriter unknown_writer;
    unknown_writer.write_u16(999);
    EXPECT_FALSE(dispatcher.dispatch(42, unknown_writer.bytes()));
}

TEST(MessageDispatcher, SendAndReceiveOverTransport) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(7777));
    ASSERT_TRUE(client.connect("127.0.0.1", 7777));

    server.poll_events([](const TransportEvent&) {});
    client.poll_events([](const TransportEvent&) {});

    MessageDispatcher server_dispatcher;
    constexpr MessageId kMoveMsgId = 20;

    glm::vec2 last_move{0.0f, 0.0f};
    server_dispatcher.register_handler(kMoveMsgId, [&](PeerId, ByteReader& reader) {
        if (auto pos = reader.read_vec2()) {
            last_move = *pos;
        }
    });

    // Client sends move command
    ByteWriter move_payload;
    move_payload.write_vec2(glm::vec2(100.0f, 250.0f));

    EXPECT_TRUE(MessageDispatcher::send(client, kServerPeerId, kMoveMsgId, move_payload.bytes()));

    // Server polls transport and passes to dispatcher
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::DataReceived) {
            EXPECT_TRUE(server_dispatcher.dispatch(event.peer_id, event.payload));
        }
    });

    EXPECT_FLOAT_EQ(last_move.x, 100.0f);
    EXPECT_FLOAT_EQ(last_move.y, 250.0f);
}

} // namespace engine::net
