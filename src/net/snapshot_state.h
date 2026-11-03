#pragma once

// docs/tech/modules/Net.md

#include "net/entity_snapshot.h"

#include <engine/net/net_tick.h>

#include <algorithm>
#include <vector>

namespace engine::net {

// The full replicated state at one tick: every entity, sorted by `NetId`.
struct SnapshotState {
    Tick tick{kNoTick};
    std::vector<EntitySnapshot> entities{};

    [[nodiscard]] const EntitySnapshot* find(NetId id) const noexcept {
        const auto it = std::lower_bound(entities.begin(), entities.end(), id,
                [](const EntitySnapshot& entity, NetId value) { return entity.id < value; });
        if (it == entities.end() || it->id != id) {
            return nullptr;
        }
        return &*it;
    }

    bool operator==(const SnapshotState&) const = default;
};

} // namespace engine::net
