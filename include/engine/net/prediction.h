#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/log.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_prediction.h>
#include <engine/net/prediction_channel.h>
#include <engine/net/prediction_desc.h>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::net {

// The inputs and predicted states of each predicted entity, a ring of `kPredictionRingTicks` ticks, and the
// game's hooks from `PredictionDesc`. On a server state for tick T it compares the state predicted for T and,
// on a mismatch, restores T and replays the stored inputs with `step` up to the present tick.
template<typename Input, typename State>
class Prediction final : public PredictionChannel {
public:
    explicit Prediction(PredictionDesc<Input, State> desc)
        : desc_(std::move(desc)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return desc_.name;
    }

    [[nodiscard]] bool valid_input(std::span<const std::uint8_t> bytes) const override {
        Input input{};
        return decode(bytes, input);
    }

    void step_encoded(ecs::World& world, ecs::Entity entity, std::span<const std::uint8_t> bytes) override {
        Input input{};
        if (!bytes.empty() && !decode(bytes, input)) {
            input = Input{};
        }
        desc_.step(world, entity, input);
    }

    void begin(NetId id, ecs::Entity entity) override {
        const auto it = tracks_.find(id);
        if (it != tracks_.end() && it->second.entity == entity) {
            return;
        }
        Track track;
        track.entity = entity;
        track.ring.resize(kPredictionRingTicks);
        tracks_.insert_or_assign(id, std::move(track));
    }

    void forget(NetId id) override {
        tracks_.erase(id);
    }

    void clear() override {
        tracks_.clear();
    }

    void drop_states() override {
        for (auto& [id, track] : tracks_) {
            track.reconciled = kNoTick;
            for (Slot& slot : track.ring) {
                slot.state.reset();
            }
        }
    }

    void predict(ecs::World& world, NetId id, Tick tick) override {
        Track* track = find(id);
        if (track == nullptr) {
            return;
        }
        Input input = desc_.collect ? desc_.collect(world, track->entity) : Input{};
        track->slot(tick) = Slot{.tick = tick, .input = input, .collected = true};
        desc_.step(world, track->entity, input);
    }

    void record(ecs::World& world, NetId id, Tick tick) override {
        Track* track = find(id);
        if (track == nullptr) {
            return;
        }
        Slot& slot = track->slot(tick);
        if (slot.tick != tick) {
            slot = Slot{.tick = tick};
        }
        slot.state = desc_.capture(world, track->entity);
    }

    [[nodiscard]] std::vector<std::vector<std::uint8_t>> inputs(
            NetId id, Tick newest, std::size_t max_count) const override {
        std::vector<std::vector<std::uint8_t>> out;
        const auto it = tracks_.find(id);
        if (it == tracks_.end()) {
            return out;
        }
        const Track& track = it->second;
        for (std::size_t i = 0; i < max_count && i < kPredictionRingTicks && newest > i; ++i) {
            const Tick tick = newest - static_cast<Tick>(i);
            const Slot& slot = track.slot(tick);
            if (slot.tick != tick || !slot.collected) {
                break;
            }
            ByteWriter writer;
            desc_.write_input(writer, *slot.input);
            out.push_back(writer.buffer());
        }
        return out;
    }

    Outcome reconcile(ecs::World& world, NetId id, Tick authoritative, Tick predicted,
            const std::function<void()>& apply_authoritative, bool& replaying) override {
        Track* track = find(id);
        if (track == nullptr || (track->reconciled != kNoTick && authoritative <= track->reconciled)) {
            return Outcome::Skipped;
        }
        track->reconciled = authoritative;
        const ecs::Entity entity = track->entity;
        const State present = desc_.capture(world, entity);
        apply_authoritative();
        const State server = desc_.capture(world, entity);

        if (authoritative > predicted) {
            keep_server_state(*track, authoritative, server);
            return Outcome::Accepted;
        }
        const Slot& at = track->slot(authoritative);
        if (at.tick == authoritative && at.state.has_value() && same(*at.state, server)) {
            desc_.restore(world, entity, present);
            return Outcome::Matched;
        }
        desc_.restore(world, entity, server);
        if (predicted - authoritative >= kPredictionRingTicks) {
            for (Slot& slot : track->ring) {
                slot = Slot{};
            }
            keep_server_state(*track, authoritative, server);
            return Outcome::Snapped;
        }
        keep_server_state(*track, authoritative, server);

        // A tick the client stored no input for repeats the last input before it, as the server does. That
        // stand-in is never sent: the server repeats the same input on its own.
        Input last = last_input_at_or_before(*track, authoritative);
        replaying = true;
        for (Tick tick = authoritative + 1; tick <= predicted; ++tick) {
            Slot& slot = track->slot(tick);
            if (slot.tick == tick && slot.input.has_value()) {
                last = *slot.input;
            } else {
                slot = Slot{.tick = tick, .input = last};
            }
            desc_.step(world, entity, last);
            slot.state = desc_.capture(world, entity);
        }
        replaying = false;
        if (desc_.corrected) {
            desc_.corrected(world, entity, present, desc_.capture(world, entity));
        }
        return Outcome::Replayed;
    }

private:
    struct Slot {
        Tick tick{kNoTick};
        std::optional<Input> input{};
        // `input` came from `collect`, not from a replay standing in for a missing one.
        bool collected{false};
        std::optional<State> state{};
    };

    struct Track {
        ecs::Entity entity{};
        Tick reconciled{kNoTick};
        std::vector<Slot> ring{};

        [[nodiscard]] Slot& slot(Tick tick) {
            return ring[tick % kPredictionRingTicks];
        }
        [[nodiscard]] const Slot& slot(Tick tick) const {
            return ring[tick % kPredictionRingTicks];
        }
    };

    [[nodiscard]] Track* find(NetId id) {
        const auto it = tracks_.find(id);
        return it != tracks_.end() ? &it->second : nullptr;
    }

    [[nodiscard]] bool decode(std::span<const std::uint8_t> bytes, Input& out) const {
        ByteReader reader(bytes);
        out = Input{};
        return desc_.read_input(reader, out) && reader.is_ok() && reader.remaining() == 0;
    }

    [[nodiscard]] bool same(const State& predicted, const State& authoritative) const {
        if (desc_.matches) {
            return desc_.matches(predicted, authoritative);
        }
        if constexpr (std::equality_comparable<State>) {
            return predicted == authoritative;
        } else {
            return false;
        }
    }

    static void keep_server_state(Track& track, Tick tick, const State& state) {
        Slot& slot = track.slot(tick);
        if (slot.tick != tick) {
            slot = Slot{.tick = tick};
        }
        slot.state = state;
    }

    [[nodiscard]] static Input last_input_at_or_before(const Track& track, Tick tick) {
        for (Tick back = 0; back < kPredictionRingTicks && tick > back; ++back) {
            const Slot& slot = track.slot(tick - back);
            if (slot.tick == tick - back && slot.input.has_value()) {
                return *slot.input;
            }
        }
        return Input{};
    }

    PredictionDesc<Input, State> desc_;
    std::map<NetId, Track> tracks_{};
};

// Gives `world` its prediction channel. Call it on the server and on every client, before `install_net_server`
// or `install_net_client` connects, with the same `name`: it is part of the schema hash in Welcome. One per
// world; a second call logs an error and is ignored (and asserts in Debug).
template<typename Input, typename State>
void register_prediction(ecs::World& world, PredictionDesc<Input, State> desc) {
    NetPrediction& slot = world.ctx<NetPrediction>();
    if (slot.channel != nullptr) {
        log::error("net: register_prediction called twice on one world; ignored");
        assert(false && "register_prediction called twice on one world");
        return;
    }
    slot.channel = std::make_unique<Prediction<Input, State>>(std::move(desc));
}

} // namespace engine::net
