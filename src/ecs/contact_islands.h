#pragma once

// Private. Union-find over body slots. No World. Not installed.

#include <engine/ecs/entity.h>

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

struct IslandBodyPair {
    int a{0};
    int b{0};
};

// `overlapping[i]` links the two bodies. A candidate whose bodies land in the same component is kept,
// in input order. Islands are ordered by the lowest entity. Bodies that never overlap are dropped.
[[nodiscard]] std::vector<std::vector<int>> group_contact_islands(std::span<const ecs::Entity> entities,
                                                                  std::span<const IslandBodyPair> links,
                                                                  std::span<const std::uint8_t> overlapping);

}
