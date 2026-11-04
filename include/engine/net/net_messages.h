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
inline constexpr MessageId kPayloadBeginMessageId = 0x0104;    // server -> client, ReliableOrdered
inline constexpr MessageId kPayloadFragmentMessageId = 0x0105; // server -> client, ReliableOrdered
inline constexpr MessageId kPayloadCancelMessageId = 0x0106;   // server -> client, ReliableOrdered
inline constexpr MessageId kPayloadAckMessageId = 0x0107;      // client -> server, ReliableOrdered

// Bumped whenever a layout in this list changes.
inline constexpr std::uint16_t kNetProtocolVersion = 3;

// The client keeps snapshots this many ticks behind its newest, and the server remembers what it wrote in them for
// as long: an ack older than this says nothing, and whatever it carried is simply written again.
inline constexpr Tick kSnapshotWindowTicks = 128;

// A snapshot above this many bytes no longer fits one UDP packet, and GameNetworkingSockets loses all of it when any
// piece is lost. It is the default of `NetServerConfig::snapshot_budget_bytes`.
inline constexpr std::size_t kSnapshotMtuBudget = 1200;
// What `NetServerConfig::snapshot_budget_bytes` is clamped to.
inline constexpr std::size_t kMinSnapshotBudget = 64;
inline constexpr std::size_t kMaxSnapshotBudget = 60000;

// Data bytes in one PayloadBegin or PayloadFragment: one reliable message per packet, and the grain at which the
// budget, a higher priority, or a cancel can step in.
inline constexpr std::size_t kPayloadFragmentBytes = 1024;

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
