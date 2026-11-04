#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_identity.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace engine::net {

// The type-erased side of `Prediction<Input, State>` that `NetServer` and `NetClient` drive.
class PredictionChannel {
public:
    enum class Outcome : std::uint8_t {
        // Nothing new: the server state is not newer than the last one reconciled.
        Skipped,
        // The server state is ahead of the prediction: taken as the present.
        Accepted,
        // The prediction matched: the present state stays.
        Matched,
        // The prediction differed: rewound to the server state and replayed.
        Replayed,
        // Too old to replay from: taken as the present, the ring cleared.
        Snapped,
    };

    PredictionChannel() = default;
    virtual ~PredictionChannel() = default;
    PredictionChannel(const PredictionChannel&) = delete;
    PredictionChannel& operator=(const PredictionChannel&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Server: whether `bytes` decode to exactly one input.
    [[nodiscard]] virtual bool valid_input(std::span<const std::uint8_t> bytes) const = 0;
    // Server: decodes `bytes` (empty: `Input{}`) and steps the entity.
    virtual void step_encoded(ecs::World& world, ecs::Entity entity, std::span<const std::uint8_t> bytes) = 0;

    // Client: starts tracking `id` as `entity`. A no-op when it already tracks that entity; a new entity
    // starts an empty ring.
    virtual void begin(NetId id, ecs::Entity entity) = 0;
    virtual void forget(NetId id) = 0;
    virtual void clear() = 0;
    // Client: the predicted tick jumped. Forgets every predicted state and what was reconciled but keeps the inputs, so
    // the next server state replays up to the new tick with the real inputs, repeating the last one as the server does.
    virtual void drop_states() = 0;
    // Client: collects the input for `tick`, stores it, and steps.
    virtual void predict(ecs::World& world, NetId id, Tick tick) = 0;
    // Client: stores the state at the end of `tick`.
    virtual void record(ecs::World& world, NetId id, Tick tick) = 0;
    // Client: the collected inputs from `newest` back, encoded, newest first, at most `max_count`, stopping at
    // the first tick without one (a replay's stand-in for a missing input does not count).
    [[nodiscard]] virtual std::vector<std::vector<std::uint8_t>> inputs(
            NetId id, Tick newest, std::size_t max_count) const = 0;
    // Client: the server's state at `authoritative` reached the entity through `apply_authoritative`; compare it
    // with the prediction and replay up to `predicted` on a mismatch. `replaying` is true during the replay.
    virtual Outcome reconcile(ecs::World& world, NetId id, Tick authoritative, Tick predicted,
            const std::function<void()>& apply_authoritative, bool& replaying) = 0;
};

} // namespace engine::net
