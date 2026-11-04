#pragma once

// docs/tech/modules/Net.md#what-a-peer-has

#include "net/entity_snapshot.h"
#include "net/snapshot_peer_header.h"

#include <engine/net/net_tick.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

namespace engine::net {

// An entity relevant to a peer at this send, from the shared capture.
struct RelevantEntity {
    const EntitySnapshot* entity{nullptr};
    std::uint32_t capture_index{0};
    // The replicated parent, `kInvalidNetId` for none. A parent that was never written goes out with its child.
    NetId parent{kInvalidNetId};
    // Never deferred: a predicted entity's reconciliation reads its state at the newest tick.
    bool mandatory{false};
};

// How much an entity competes for a snapshot, asked only when a snapshot does not fit everything.
using EntityWeight = std::function<float(const RelevantEntity&)>;

// What one peer has been sent and has acknowledged, entity by entity. There is no whole-state baseline: a snapshot
// that leaves entities out would make them step back to an older state if the client decoded it against one. Every
// slot value is self-contained and the client applies records in tick order, so a slot is clean once a snapshot that
// wrote the entity, no older than the slot's current value, is acknowledged, and the server only writes what is dirty.
class PeerGhosts {
public:
    struct Ghost {
        NetId id{kInvalidNetId};
        // The entity as last written; a slot a record left out already had this value.
        EntitySnapshot latest{};
        bool written{false};
        // The tick each slot's current value (or absence) was first written. Slots past `since.size()` never had one.
        std::vector<Tick> since{};
        Tick owner_since{kNoTick};
        Tick max_since{kNoTick};
        // The newest acknowledged snapshot that wrote this entity.
        Tick confirmed{kNoTick};
        // Grows while the entity is dirty and left out; reset by a write.
        float accumulator{0.0f};
        bool leaving{false};
        Tick leave_since{kNoTick};
        Tick last_listed{kNoTick};
    };

    struct Built {
        std::vector<std::uint8_t> bytes{};
        std::size_t relevant{0};
        std::size_t deferred{0};
    };

    // Brings the table up to `relevant` (sorted by id) and builds this tick's Snapshot message of at most `budget`
    // bytes. Mandatory entities and the entity at the top of the priority order go out even above it.
    [[nodiscard]] Built build(Tick tick, const SnapshotPeerHeader& header, std::span<const RelevantEntity> relevant,
            std::size_t budget, std::size_t slot_count, const EntityWeight& weight);

    // The peer holds snapshot `tick`. Ignored unless that snapshot was sent and is still inside the window.
    void acknowledge(Tick tick);

    // The newest snapshot acknowledged, `kNoTick` for none.
    [[nodiscard]] Tick acked() const noexcept {
        return acked_;
    }
    // Sorted by id; includes entities still leaving.
    [[nodiscard]] const std::vector<Ghost>& ghosts() const noexcept {
        return ghosts_;
    }

private:
    struct Sent {
        Tick tick{kNoTick};
        std::vector<NetId> written{};
        std::vector<NetId> removed{};
    };

    [[nodiscard]] Ghost* find(NetId id) noexcept;

    std::vector<Ghost> ghosts_{};
    std::deque<Sent> sent_{};
    Tick acked_{kNoTick};
};

} // namespace engine::net
