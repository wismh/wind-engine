#pragma once

// docs/tech/modules/Net.md (Fluids)

#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_transport.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace engine {
class JobSystem;
}

namespace engine::net {

// Names a Fluid2D or an SdfCollider2D the same on every peer. Put it on the entity on every peer (a level loaded from
// the same file does): the fluid stream addresses fluids and terrains by `id`, never by entity.
struct FluidSyncId2D {
    std::uint32_t id = 0;
};

// What changes a synced fluid or its terrain, applied by every peer at the tick the server applied it, in server order.
struct FluidInput2D {
    enum class Kind : std::uint8_t {
        Spawn,          // fluid `target`: one particle at `a` with velocity `b`
        SpawnBox,       // fluid `target`: spawn_box(a, b) with velocity `velocity`
        Clear,          // fluid `target`: every particle removed
        SourceEnabled,  // fluid `target`: sources[index].enabled = value != 0
        SourceRate,     // fluid `target`: sources[index].rate = value
        CarveCircle,    // terrain `target`: carve_circle(a, value)
        FillCircle,     // terrain `target`: fill_circle(a, value)
        CarvePolygon,   // terrain `target`: carve_polygon(polygon)
        FillPolygon,    // terrain `target`: fill_polygon(polygon)
    };
    Kind kind = Kind::Spawn;
    std::uint32_t target = 0;  // a FluidSyncId2D::id
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
    glm::vec2 velocity{0.0f};
    float value = 0.0f;
    std::uint32_t index = 0;
    std::vector<glm::vec2> polygon;

    bool operator==(const FluidInput2D&) const = default;
};

// The game's message ids for the fluid stream: outside the engine range (0x0100-0x01FF), unused by the game's own.
struct FluidSyncConfig2D {
    MessageId tick_message = 0xF101;    // server to clients, reliable: one fluid step
    MessageId state_message = 0xF102;   // server to a client, reliable: the whole fluid and terrain state
    MessageId resync_message = 0xF103;  // client to server: send me the state again
    // Every this many ticks a tick message carries each synced fluid's state_hash for the client to check.
    std::uint32_t hash_interval = 30;
    JobSystem* jobs = nullptr;
};

// The server side. Registers one system on Fixed / Physics, appended after what is there (call it after the engine's
// systems are registered: a game's on_start), that each tick applies the inputs queued since the last tick, records
// the movable colliders the fluid sees (`movable_fluid_boundaries_2d`), steps every fluid (run_fluid_2d, pushing
// bodies), and sends every client one reliable tick message: the tick, those inputs, those colliders and, every
// `hash_interval` ticks, the fluids' hashes. A client that connects, or asks for a resync, first gets a state message
// of that tick. Replaces install_fluid_2d on the server: do not install both.
void install_fluid_sync_server_2d(ecs::World& world, INetworkTransport& transport, FluidSyncConfig2D config = {});

// Queues `input` on the server for the next fluid step: applied there and in every client's copy at that tick.
// The way a game changes a synced fluid or terrain; a direct change desyncs the clients.
void queue_fluid_input_2d(ecs::World& world, FluidInput2D input);

// The client side. Registers one system on Frame / NetReceive, after run_net_client_interpolate (call it after
// install_net_client). The client keeps no fluid of its own on the fixed clock: it steps its fluids, once per server
// tick, when its render tick reaches that tick, with the inputs and colliders of that tick's message and without
// pushing bodies, so what it draws matches the replicated bodies on screen. Ticks before the first state message are
// dropped. A hash that differs logs a warning and asks the server for the state again.
void install_fluid_sync_client_2d(ecs::World& world, INetworkTransport& transport, FluidSyncConfig2D config = {});

// The client's progress: the last server tick its fluids are at (kNoTick before the first state), how many hash
// mismatches it saw and how many states it loaded.
struct FluidSyncClientStats2D {
    Tick synced_tick = kNoTick;
    std::uint32_t desyncs = 0;
    std::uint32_t states_loaded = 0;
    std::size_t queued_ticks = 0;
};
[[nodiscard]] FluidSyncClientStats2D fluid_sync_client_stats_2d(const ecs::World& world);

}
