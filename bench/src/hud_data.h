#pragma once

#include "bench_random.h"
#include "unit_marker.h"

#include <glm/vec2.hpp>

#include <cstddef>
#include <vector>

namespace bench {

// The HUD scene's world: its size in pixels, the grid step, the units on it, and the teams they belong to.
inline constexpr glm::vec2 kWorldSize{2400.0f, 1600.0f};
inline constexpr float kWorldGrid = 64.0f;
inline constexpr std::size_t kWorldUnits = 400;
inline constexpr int kTeams = 4;

// `count` units inside `world`, teams 0..kTeams-1.
[[nodiscard]] std::vector<UnitMarker> make_unit_markers(std::size_t count, glm::vec2 world,
        std::uint64_t seed = kBenchSeed);

}
