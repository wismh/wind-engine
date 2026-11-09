#pragma once

// Private to the engine and its tests. docs/tech/modules/ECS.md (2D fluids)

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {

class JobSystem;

// Neighbours kept per particle by the fluid solver.
inline constexpr std::size_t kFluidMaxNeighbors2D = 48;

// The fluid solver's neighbour lists: particle i's neighbours are list[i * kFluidMaxNeighbors2D, + count[i]).
struct FluidNeighbors2D {
    std::vector<std::uint32_t> count;
    std::vector<std::uint32_t> list;
};

// Runs the solver's spatial-hash neighbour search (every other particle closer than `h`) on `positions`.
[[nodiscard]] FluidNeighbors2D fluid_neighbors_2d(std::span<const glm::vec2> positions, float h, JobSystem* jobs);

}
