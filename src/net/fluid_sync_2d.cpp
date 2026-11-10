#include <engine/net/fluid_sync_2d.h>

#include <engine/ecs/events.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/log.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_client.h>
#include <engine/net/net_peer_connected_event.h>
#include <engine/net/net_peer_disconnected_event.h>
#include <engine/net/net_server.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <span>
#include <utility>
#include <variant>
#include <vector>

// docs/tech/modules/Net.md (Fluids). The fluid is simulated by every peer from the same inputs; this is the stream
// that hands them the inputs, the movable colliders of each tick, the state to start from and the hashes to check.

namespace engine::net {
namespace {

// Bounds on what one message may claim, so a malformed one cannot make the reader allocate without limit.
constexpr std::uint32_t kMaxInputs = 4096;
constexpr std::uint32_t kMaxPolygon = 4096;
constexpr std::uint32_t kMaxBoundaries = 65536;
constexpr std::uint32_t kMaxSynced = 4096;
constexpr std::uint32_t kMaxChunks = 1u << 20;

// --- Wire helpers.

void write_input(ByteWriter& out, const FluidInput2D& input) {
    out.write_u8(static_cast<std::uint8_t>(input.kind));
    out.write_u32(input.target);
    out.write_vec2(input.a);
    out.write_vec2(input.b);
    out.write_vec2(input.velocity);
    out.write_float(input.value);
    out.write_u32(input.index);
    out.write_u32(static_cast<std::uint32_t>(input.polygon.size()));
    for (const glm::vec2 v : input.polygon) {
        out.write_vec2(v);
    }
}

[[nodiscard]] std::optional<FluidInput2D> read_input(ByteReader& in) {
    FluidInput2D input;
    const auto kind = in.read_u8();
    const auto target = in.read_u32();
    const auto a = in.read_vec2();
    const auto b = in.read_vec2();
    const auto velocity = in.read_vec2();
    const auto value = in.read_float();
    const auto index = in.read_u32();
    const auto count = in.read_u32();
    if (!kind || *kind > static_cast<std::uint8_t>(FluidInput2D::Kind::FillPolygon) || !target || !a || !b ||
        !velocity || !value || !index || !count || *count > kMaxPolygon) {
        return std::nullopt;
    }
    input.kind = static_cast<FluidInput2D::Kind>(*kind);
    input.target = *target;
    input.a = *a;
    input.b = *b;
    input.velocity = *velocity;
    input.value = *value;
    input.index = *index;
    input.polygon.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto v = in.read_vec2();
        if (!v) {
            return std::nullopt;
        }
        input.polygon.push_back(*v);
    }
    return input;
}

void write_boundary(ByteWriter& out, const FluidBoundary2D& boundary) {
    out.write_u8(static_cast<std::uint8_t>(boundary.kind));
    out.write_u32(boundary.layer);
    out.write_u32(boundary.mask);
    if (boundary.kind == FluidBoundary2D::Kind::Polygon) {
        out.write_u8(boundary.count);
        for (std::uint8_t i = 0; i < boundary.count; ++i) {
            out.write_vec2(boundary.vertices[i]);
        }
        return;
    }
    out.write_vec2(boundary.position);
    out.write_vec2(boundary.axis);
    if (boundary.kind == FluidBoundary2D::Kind::Box) {
        out.write_vec2(boundary.half);
    } else {
        out.write_float(boundary.radius);
    }
}

[[nodiscard]] std::optional<FluidBoundary2D> read_boundary(ByteReader& in) {
    FluidBoundary2D boundary;
    const auto kind = in.read_u8();
    const auto layer = in.read_u32();
    const auto mask = in.read_u32();
    if (!kind || *kind > static_cast<std::uint8_t>(FluidBoundary2D::Kind::Polygon) || !layer || !mask) {
        return std::nullopt;
    }
    boundary.kind = static_cast<FluidBoundary2D::Kind>(*kind);
    boundary.layer = *layer;
    boundary.mask = *mask;
    if (boundary.kind == FluidBoundary2D::Kind::Polygon) {
        const auto count = in.read_u8();
        if (!count || *count > boundary.vertices.size()) {
            return std::nullopt;
        }
        boundary.count = *count;
        for (std::uint8_t i = 0; i < boundary.count; ++i) {
            const auto v = in.read_vec2();
            if (!v) {
                return std::nullopt;
            }
            boundary.vertices[i] = *v;
        }
        return boundary;
    }
    const auto position = in.read_vec2();
    const auto axis = in.read_vec2();
    if (!position || !axis) {
        return std::nullopt;
    }
    boundary.position = *position;
    boundary.axis = *axis;
    if (boundary.kind == FluidBoundary2D::Kind::Box) {
        const auto half = in.read_vec2();
        if (!half) {
            return std::nullopt;
        }
        boundary.half = *half;
    } else {
        const auto radius = in.read_float();
        if (!radius) {
            return std::nullopt;
        }
        boundary.radius = *radius;
    }
    return boundary;
}

// --- Synced objects by id.

template <class T>
[[nodiscard]] std::vector<std::pair<std::uint32_t, ecs::Entity>> synced(ecs::World& world) {
    std::vector<std::pair<std::uint32_t, ecs::Entity>> out;
    for (const ecs::Entity entity : world.view<FluidSyncId2D, T>()) {
        out.emplace_back(world.get<FluidSyncId2D>(entity).id, entity);
    }
    std::sort(out.begin(), out.end());
    return out;
}

template <class T>
[[nodiscard]] T* find_synced(ecs::World& world, std::uint32_t id, ecs::Entity* entity = nullptr) {
    for (const ecs::Entity candidate : world.view<FluidSyncId2D, T>()) {
        if (world.get<FluidSyncId2D>(candidate).id == id) {
            if (entity != nullptr) {
                *entity = candidate;
            }
            return &world.get<T>(candidate);
        }
    }
    return nullptr;
}

// Applies one input to this peer's fluids or terrain. An input naming nothing this peer has is skipped (and logged).
void apply_input(ecs::World& world, const FluidInput2D& input) {
    using Kind = FluidInput2D::Kind;
    const bool terrain = input.kind == Kind::CarveCircle || input.kind == Kind::FillCircle ||
                         input.kind == Kind::CarvePolygon || input.kind == Kind::FillPolygon;
    if (terrain) {
        ecs::Entity entity{};
        SdfCollider2D* sdf = find_synced<SdfCollider2D>(world, input.target, &entity);
        if (sdf == nullptr) {
            log::warn(std::format("Fluid sync: no terrain {} for an input", input.target));
            return;
        }
        switch (input.kind) {
        case Kind::CarveCircle:
            (void)sdf->field.carve_circle(input.a, input.value);
            break;
        case Kind::FillCircle:
            (void)sdf->field.fill_circle(input.a, input.value);
            break;
        case Kind::CarvePolygon:
            (void)sdf->field.carve_polygon(input.polygon);
            break;
        default:
            (void)sdf->field.fill_polygon(input.polygon);
            break;
        }
        mark_static_colliders_dirty_2d(world, entity);
        return;
    }
    Fluid2D* fluid = find_synced<Fluid2D>(world, input.target);
    if (fluid == nullptr) {
        log::warn(std::format("Fluid sync: no fluid {} for an input", input.target));
        return;
    }
    switch (input.kind) {
    case Kind::Spawn:
        (void)fluid->spawn(input.a, input.b);
        break;
    case Kind::SpawnBox:
        (void)fluid->spawn_box(input.a, input.b, input.velocity);
        break;
    case Kind::Clear:
        fluid->clear();
        break;
    case Kind::SourceEnabled:
        if (input.index < fluid->sources.size()) {
            fluid->sources[input.index].enabled = input.value != 0.0f;
        }
        break;
    case Kind::SourceRate:
        if (input.index < fluid->sources.size()) {
            fluid->sources[input.index].rate = input.value;
        }
        break;
    default:
        break;
    }
}

// The whole synced state: each fluid's save_state and each terrain's field, by id.
[[nodiscard]] std::vector<std::uint8_t> encode_state(ecs::World& world, Tick tick) {
    ByteWriter out;
    out.write_u32(tick);
    const auto fluids = synced<Fluid2D>(world);
    out.write_u32(static_cast<std::uint32_t>(fluids.size()));
    for (const auto& [id, entity] : fluids) {
        out.write_u32(id);
        out.write_bytes(world.get<Fluid2D>(entity).save_state());
    }
    const auto terrains = synced<SdfCollider2D>(world);
    out.write_u32(static_cast<std::uint32_t>(terrains.size()));
    std::vector<std::uint8_t> samples;
    for (const auto& [id, entity] : terrains) {
        const SdfField2D& field = world.get<SdfCollider2D>(entity).field;
        out.write_u32(id);
        out.write_float(field.cell_size());
        out.write_float(field.band());
        out.write_vec2(field.origin());
        out.write_u8(field.outside() == SdfOutside::Solid ? 1 : 0);
        const std::vector<glm::ivec2> coords = field.chunk_coords();
        out.write_u32(static_cast<std::uint32_t>(coords.size()));
        for (const glm::ivec2 coord : coords) {
            out.write_i32(coord.x);
            out.write_i32(coord.y);
            const std::span<const std::int16_t> chunk = field.chunk_samples(coord);
            samples.resize(chunk.size() * 2);
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                const auto value = static_cast<std::uint16_t>(chunk[i]);
                samples[2 * i] = static_cast<std::uint8_t>(value & 0xFFu);
                samples[2 * i + 1] = static_cast<std::uint8_t>(value >> 8);
            }
            out.write_bytes(samples);
        }
    }
    return std::vector<std::uint8_t>(out.span().begin(), out.span().end());
}

// --- Server.

struct FluidSyncServer2D {
    INetworkTransport* transport = nullptr;
    FluidSyncConfig2D config{};
    std::vector<FluidInput2D> queue;
    std::vector<PeerId> ready;     // peers that have a state and get every tick
    std::vector<PeerId> want;      // peers owed a state after this tick (connected, or asked for a resync)
    ecs::EventCursor<NetPeerConnectedEvent> connected{};
    ecs::EventCursor<NetPeerDisconnectedEvent> disconnected{};
};

// The server's resync handler.
void register_server_handler(ecs::World& world, NetServer& server) {
    server.dispatcher().register_handler(world.ctx<FluidSyncServer2D>().config.resync_message,
                                         [&world](PeerId from, ByteReader&) {
        FluidSyncServer2D& state = world.ctx<FluidSyncServer2D>();
        if (std::find(state.want.begin(), state.want.end(), from) == state.want.end()) {
            state.want.push_back(from);
        }
    });
}

void run_fluid_sync_server(ecs::World& world) {
    FluidSyncServer2D& sync = world.ctx<FluidSyncServer2D>();
    NetServer* server = world.try_ctx<NetServer>();
    for (const NetPeerConnectedEvent& event : ecs::EventReader<NetPeerConnectedEvent>{world, sync.connected}) {
        if (std::find(sync.want.begin(), sync.want.end(), event.peer_id) == sync.want.end()) {
            sync.want.push_back(event.peer_id);
        }
    }
    for (const NetPeerDisconnectedEvent& event : ecs::EventReader<NetPeerDisconnectedEvent>{world, sync.disconnected}) {
        std::erase(sync.ready, event.peer_id);
        std::erase(sync.want, event.peer_id);
    }
    const Tick tick = server != nullptr ? server->tick() : kNoTick;

    std::vector<FluidInput2D> inputs = std::move(sync.queue);
    sync.queue.clear();
    for (const FluidInput2D& input : inputs) {
        apply_input(world, input);
    }
    const std::vector<FluidBoundary2D> movable = movable_fluid_boundaries_2d(world);
    run_fluid_2d(world, sync.config.jobs);

    if (sync.transport == nullptr || (sync.ready.empty() && sync.want.empty())) {
        return;
    }
    ByteWriter out;
    out.write_u32(tick);
    out.write_u32(static_cast<std::uint32_t>(inputs.size()));
    for (const FluidInput2D& input : inputs) {
        write_input(out, input);
    }
    out.write_u32(static_cast<std::uint32_t>(movable.size()));
    for (const FluidBoundary2D& boundary : movable) {
        write_boundary(out, boundary);
    }
    const bool hashed = sync.config.hash_interval > 0 && tick % sync.config.hash_interval == 0;
    out.write_bool(hashed);
    if (hashed) {
        const auto fluids = synced<Fluid2D>(world);
        out.write_u32(static_cast<std::uint32_t>(fluids.size()));
        for (const auto& [id, entity] : fluids) {
            out.write_u32(id);
            out.write_u64(world.get<Fluid2D>(entity).state_hash());
        }
    }
    for (const PeerId peer : sync.ready) {
        if (std::find(sync.want.begin(), sync.want.end(), peer) == sync.want.end()) {
            (void)MessageDispatcher::send(*sync.transport, peer, sync.config.tick_message, out.span());
        }
    }
    if (!sync.want.empty()) {
        // The state after this tick: the peers that get it take the next tick's message, not this one.
        const std::vector<std::uint8_t> state = encode_state(world, tick);
        for (const PeerId peer : sync.want) {
            (void)MessageDispatcher::send(*sync.transport, peer, sync.config.state_message, state);
            if (std::find(sync.ready.begin(), sync.ready.end(), peer) == sync.ready.end()) {
                sync.ready.push_back(peer);
            }
        }
        sync.want.clear();
    }
}

// --- Client.

struct TickItem {
    Tick tick = kNoTick;
    std::vector<FluidInput2D> inputs;
    std::vector<FluidBoundary2D> movable;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> hashes;
};

struct StateItem {
    Tick tick = kNoTick;
    std::vector<std::uint8_t> bytes;
};

struct FluidSyncClient2D {
    INetworkTransport* transport = nullptr;
    FluidSyncConfig2D config{};
    std::deque<std::variant<TickItem, StateItem>> queue;
    bool has_state = false;  // a state message has arrived (ticks before it are dropped)
    bool asked = false;      // a resync is on its way
    FluidSyncClientStats2D stats{};
};

[[nodiscard]] Tick tick_of(const std::variant<TickItem, StateItem>& item) {
    return std::visit([](const auto& value) { return value.tick; }, item);
}

void ask_resync(FluidSyncClient2D& sync) {
    if (sync.asked || sync.transport == nullptr) {
        return;
    }
    sync.asked = true;
    (void)MessageDispatcher::send(*sync.transport, kServerPeerId, sync.config.resync_message, {});
}

[[nodiscard]] bool load_state(ecs::World& world, std::span<const std::uint8_t> bytes) {
    ByteReader in(bytes);
    if (!in.read_u32()) {
        return false;
    }
    const auto fluids = in.read_u32();
    if (!fluids || *fluids > kMaxSynced) {
        return false;
    }
    for (std::uint32_t k = 0; k < *fluids; ++k) {
        const auto id = in.read_u32();
        const auto size = in.read_u32();
        if (!id || !size) {
            return false;
        }
        const auto state = in.read_bytes(*size);
        if (!state) {
            return false;
        }
        Fluid2D* fluid = find_synced<Fluid2D>(world, *id);
        if (fluid == nullptr || !fluid->load_state(*state)) {
            return false;
        }
    }
    const auto terrains = in.read_u32();
    if (!terrains || *terrains > kMaxSynced) {
        return false;
    }
    for (std::uint32_t k = 0; k < *terrains; ++k) {
        const auto id = in.read_u32();
        const auto cell = in.read_float();
        const auto band = in.read_float();
        const auto origin = in.read_vec2();
        const auto outside = in.read_u8();
        const auto chunks = in.read_u32();
        if (!id || !cell || !band || !origin || !outside || !chunks || *chunks > kMaxChunks) {
            return false;
        }
        SdfField2D field{SdfField2DDesc{.cell_size = *cell,
                                        .band = *band,
                                        .origin = *origin,
                                        .outside = *outside != 0 ? SdfOutside::Solid : SdfOutside::Empty}};
        std::vector<std::int16_t> samples(static_cast<std::size_t>(kSdfChunkSamples));
        for (std::uint32_t c = 0; c < *chunks; ++c) {
            const auto x = in.read_i32();
            const auto y = in.read_i32();
            const auto size = in.read_u32();
            if (!x || !y || !size || *size != samples.size() * 2) {
                return false;
            }
            const auto raw = in.read_bytes(*size);
            if (!raw) {
                return false;
            }
            for (std::size_t i = 0; i < samples.size(); ++i) {
                samples[i] = static_cast<std::int16_t>(
                        static_cast<std::uint16_t>((*raw)[2 * i]) | (static_cast<std::uint16_t>((*raw)[2 * i + 1]) << 8));
            }
            (void)field.assign_chunk({*x, *y}, samples);
        }
        ecs::Entity entity{};
        SdfCollider2D* sdf = find_synced<SdfCollider2D>(world, *id, &entity);
        if (sdf == nullptr) {
            return false;
        }
        sdf->field = std::move(field);
        mark_static_colliders_dirty_2d(world, entity);
    }
    return in.remaining() == 0;
}

// The client's handlers, on the NetClient's dispatcher (installed before this), so no message is missed.
void register_client_handlers(ecs::World& world, NetClient& client) {
    FluidSyncClient2D& sync = world.ctx<FluidSyncClient2D>();
    client.dispatcher().register_handler(sync.config.tick_message, [&world](PeerId, ByteReader& in) {
        FluidSyncClient2D& state = world.ctx<FluidSyncClient2D>();
        TickItem item;
        const auto tick = in.read_u32();
        const auto inputs = in.read_u32();
        if (!tick || !inputs || *inputs > kMaxInputs) {
            log::warn("Fluid sync: a malformed tick message");
            ask_resync(state);
            return;
        }
        item.tick = *tick;
        for (std::uint32_t i = 0; i < *inputs; ++i) {
            auto input = read_input(in);
            if (!input) {
                log::warn("Fluid sync: a malformed tick message");
                ask_resync(state);
                return;
            }
            item.inputs.push_back(std::move(*input));
        }
        const auto boundaries = in.read_u32();
        if (!boundaries || *boundaries > kMaxBoundaries) {
            ask_resync(state);
            return;
        }
        for (std::uint32_t i = 0; i < *boundaries; ++i) {
            auto boundary = read_boundary(in);
            if (!boundary) {
                ask_resync(state);
                return;
            }
            item.movable.push_back(*boundary);
        }
        const auto hashed = in.read_bool();
        if (!hashed) {
            ask_resync(state);
            return;
        }
        if (*hashed) {
            const auto count = in.read_u32();
            if (!count || *count > kMaxSynced) {
                ask_resync(state);
                return;
            }
            for (std::uint32_t i = 0; i < *count; ++i) {
                const auto id = in.read_u32();
                const auto hash = in.read_u64();
                if (!id || !hash) {
                    ask_resync(state);
                    return;
                }
                item.hashes.emplace_back(*id, *hash);
            }
        }
        if (state.has_state) {
            state.queue.emplace_back(std::move(item));
        }
    });
    client.dispatcher().register_handler(sync.config.state_message, [&world](PeerId, ByteReader& in) {
        FluidSyncClient2D& state = world.ctx<FluidSyncClient2D>();
        StateItem item;
        const auto rest = in.read_bytes(in.remaining());
        if (!rest || rest->size() < 4) {
            return;
        }
        ByteReader head(*rest);
        item.tick = *head.read_u32();
        item.bytes.assign(rest->begin(), rest->end());
        // A state replaces whatever ticks were queued before it: they lead to it.
        std::erase_if(state.queue, [&](const auto& queued) { return tick_of(queued) <= item.tick; });
        state.queue.emplace_back(std::move(item));
        state.has_state = true;
    });
}

void run_fluid_sync_client(ecs::World& world) {
    FluidSyncClient2D& sync = world.ctx<FluidSyncClient2D>();
    NetClient* client = world.try_ctx<NetClient>();
    if (client == nullptr) {
        return;
    }
    const double render = client->render_tick();
    while (!sync.queue.empty() && static_cast<double>(tick_of(sync.queue.front())) <= render) {
        auto item = std::move(sync.queue.front());
        sync.queue.pop_front();
        if (auto* state = std::get_if<StateItem>(&item)) {
            if (!load_state(world, state->bytes)) {
                log::warn("Fluid sync: a state that does not fit this world");
                continue;
            }
            sync.stats.synced_tick = state->tick;
            ++sync.stats.states_loaded;
            sync.asked = false;
            continue;
        }
        TickItem& tick = std::get<TickItem>(item);
        if (sync.stats.synced_tick != kNoTick && tick.tick != sync.stats.synced_tick + 1) {
            log::warn(std::format("Fluid sync: tick {} after {}", tick.tick, sync.stats.synced_tick));
            ask_resync(sync);
        }
        for (const FluidInput2D& input : tick.inputs) {
            apply_input(world, input);
        }
        run_fluid_2d(world, sync.config.jobs,
                     FluidStepOptions2D{.push_bodies = false, .movable_from_world = false, .movable = tick.movable});
        sync.stats.synced_tick = tick.tick;
        for (const auto& [id, hash] : tick.hashes) {
            const Fluid2D* fluid = find_synced<Fluid2D>(world, id);
            if (fluid != nullptr && fluid->state_hash() != hash) {
                ++sync.stats.desyncs;
                log::warn(std::format("Fluid sync: fluid {} differs from the server at tick {}", id, tick.tick));
                ask_resync(sync);
            }
        }
    }
    sync.stats.queued_ticks = sync.queue.size();
}

}

void install_fluid_sync_server_2d(ecs::World& world, INetworkTransport& transport, FluidSyncConfig2D config) {
    FluidSyncServer2D& sync = world.ctx<FluidSyncServer2D>();
    const bool first = sync.transport == nullptr;
    sync.transport = &transport;
    sync.config = config;
    if (first) {
        world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics, run_fluid_sync_server);
        if (NetServer* server = world.try_ctx<NetServer>()) {
            register_server_handler(world, *server);
        } else {
            log::warn("install_fluid_sync_server_2d: call it after install_net_server");
        }
    }
}

void queue_fluid_input_2d(ecs::World& world, FluidInput2D input) {
    world.ctx<FluidSyncServer2D>().queue.push_back(std::move(input));
}

void install_fluid_sync_client_2d(ecs::World& world, INetworkTransport& transport, FluidSyncConfig2D config) {
    FluidSyncClient2D& sync = world.ctx<FluidSyncClient2D>();
    const bool first = sync.transport == nullptr;
    sync.transport = &transport;
    sync.config = config;
    if (first) {
        world.add_system(ecs::Schedule::Frame, ecs::Phase::NetReceive, run_fluid_sync_client);
        if (NetClient* client = world.try_ctx<NetClient>()) {
            register_client_handlers(world, *client);
        } else {
            log::warn("install_fluid_sync_client_2d: call it after install_net_client");
        }
    }
}

FluidSyncClientStats2D fluid_sync_client_stats_2d(const ecs::World& world) {
    const FluidSyncClient2D* sync = world.try_ctx<FluidSyncClient2D>();
    return sync != nullptr ? sync->stats : FluidSyncClientStats2D{};
}

}
