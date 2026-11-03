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

// Bumped whenever the Welcome or Snapshot layout changes.
inline constexpr std::uint16_t kNetProtocolVersion = 1;

// A baseline older than this many ticks behind the newest is gone on both sides: the server sends a full
// snapshot instead, the client drops it from its buffer.
inline constexpr Tick kSnapshotWindowTicks = 128;

// A snapshot above this many bytes no longer fits one UDP packet. It is still sent (GameNetworkingSockets
// splits it, and loses all of it when any piece is lost); the server logs one warning per peer.
inline constexpr std::size_t kSnapshotMtuBudget = 1200;

} // namespace engine::net
