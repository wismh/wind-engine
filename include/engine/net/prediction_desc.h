#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>

#include <functional>
#include <string>

namespace engine::net {

// How a game's controlled entities take input and are predicted. Registered with `register_prediction` on the
// server and on every client, with the same `name`. An entity is controlled when a client peer owns it
// (`replicate(world, entity, peer)`): that peer's inputs drive it on the server, and that client predicts it.
//
// `State` is everything `step` writes, and every part of it must be replicated: the client compares it with
// the server's after applying a snapshot's components. A replicated component outside `State` takes the
// newest snapshot's value on a predicted entity.
template<typename Input, typename State>
struct PredictionDesc {
    // Part of the schema hash in Welcome.
    std::string name{};
    // Encode and decode one input. `read_input` starts from `Input{}`; false on bad bytes. The decoded input
    // must use every byte.
    std::function<void(ByteWriter&, const Input&)> write_input{};
    std::function<bool(ByteReader&, Input&)> read_input{};
    // One fixed tick of the entity with `input`. The server runs it for each controlled entity every tick; the
    // client runs it for each predicted tick and again when it replays. It must not send events or create
    // entities while `NetClient::replaying()` is true.
    std::function<void(ecs::World&, ecs::Entity, const Input&)> step{};
    // Client: this tick's input, from `InputSystem` through `ActionId`s.
    std::function<Input(ecs::World&, ecs::Entity)> collect{};
    // Client: read and write the entity's predicted state.
    std::function<State(const ecs::World&, ecs::Entity)> capture{};
    std::function<void(ecs::World&, ecs::Entity, const State&)> restore{};
    // Client: whether a prediction is close enough to the server's state. Empty: `operator==` when `State` has
    // one, otherwise never (every server state replays).
    std::function<bool(const State& predicted, const State& authoritative)> matches{};
    // Client, optional: after a replay, the state before it and after it, for visual smoothing.
    std::function<void(ecs::World&, ecs::Entity, const State& before, const State& after)> corrected{};
};

} // namespace engine::net
