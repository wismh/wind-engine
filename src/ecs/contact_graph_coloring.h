#pragma once

// Private. Greedy graph coloring for 2D and 3D contact constraints. No World. Not installed.

#include "contact_islands.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

inline constexpr int kDefaultMaxGraphColors = 24;

struct ContactColorGraph {
    // Each color bucket holds indices into the input `constraints` span.
    // Within a single color, no two constraints share a writable dynamic body.
    std::vector<std::vector<int>> colors{};
    // Constraints that could not fit into `max_colors` (e.g. extreme high-degree hubs),
    // to be solved sequentially after the colored passes.
    std::vector<int> overflow{};
};

// Partitions `constraints` into independent color sets using bitset occupancy per body.
// `writable_body[b]` is non-zero if body `b` is an awake dynamic body written by the solver.
// Static, kinematic, and frozen sleeping bodies (`writable_body[b] == 0`) are read-only and
// do not create conflict edges between constraints.
[[nodiscard]] ContactColorGraph build_contact_color_graph(int body_count,
                                                          std::span<const std::uint8_t> writable_body,
                                                          std::span<const IslandBodyPair> constraints,
                                                          int max_colors = kDefaultMaxGraphColors);

}
