#pragma once

// docs/tech/modules/Net.md

#include <engine/net/message_dispatcher.h>
#include <engine/net/net_tick.h>

#include <cstddef>
#include <cstdint>

namespace engine::net {

// Message ids 0x0100-0x01FF are the engine's. A game registers its own messages outside that range.
inline constexpr MessageId kEngineMessageFirst = 0x0100;
inline constexpr MessageId kEngineMessageLast = 0x01FF;

inline constexpr MessageId kWelcomeMessageId = 0x0100;     // server -> client, ReliableOrdered
inline constexpr MessageId kSnapshotMessageId = 0x0101;    // server -> client, UnreliableImmediate
inline constexpr MessageId kSnapshotAckMessageId = 0x0102; // client -> server, Unreliable
inline constexpr MessageId kInputMessageId = 0x0103;       // client -> server, UnreliableImmediate

// Bumped whenever the Welcome, Snapshot, or Input layout changes.
inline constexpr std::uint16_t kNetProtocolVersion = 2;

// A baseline older than this many ticks behind the newest is gone on both sides: the server sends a full
// snapshot instead, the client drops it from its buffer.
inline constexpr Tick kSnapshotWindowTicks = 128;

// A snapshot above this many bytes no longer fits one UDP packet. It is still sent (GameNetworkingSockets
// splits it, and loses all of it when any piece is lost); the server logs one warning per peer.
inline constexpr std::size_t kSnapshotMtuBudget = 1200;

// The server ignores an input for a tick more than this many ticks after its own.
inline constexpr Tick kMaxInputLeadTicks = 64;
// Ticks of inputs and predicted states a client keeps per predicted entity. A server state older than this
// cannot be replayed from, so the client takes it as is.
inline constexpr Tick kPredictionRingTicks = 128;
// Caps of one Input message: inputs per entity, entities, and bytes per encoded input.
inline constexpr std::size_t kMaxInputsPerMessage = 32;
inline constexpr std::size_t kMaxInputEntities = 8;
inline constexpr std::size_t kMaxInputBytes = 256;
// `input_slack` in a snapshot when the server has no input from that peer yet.
inline constexpr std::int8_t kNoInputSlack = -128;

} // namespace engine::net
