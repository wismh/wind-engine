#pragma once

// Private to the engine and its tests. docs/tech/modules/ECS.md (2D fluids)

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {

class JobSystem;

namespace ecs {
class World;
}

// `install_fluid_2d`'s marker in `world.ctx`. `jobs` is read by the system each tick, so a second install moves it.
struct FluidInstall2D {
    JobSystem* jobs = nullptr;
    bool registered = false;
};

// Adds `run_fluid_2d` to Fixed / Physics once, when `install_fluid_2d` has run on `world`. Called by
// `install_fluid_2d` when the simulation systems are already registered, and by `register_simulation_systems` right
// after `run_physics_3d` otherwise.
void register_fluid_2d_system(ecs::World& world);

// Neighbours kept per particle by the fluid solver.
inline constexpr std::size_t kFluidMaxNeighbors2D = 48;

// The fluid solver's neighbour lists: particle i's neighbours are list[i * kFluidMaxNeighbors2D, + count[i]).
struct FluidNeighbors2D {
    std::vector<std::uint32_t> count;
    std::vector<std::uint32_t> list;
};

// The solver's hot loops (lambda, delta) run four particles per SSE2 register on x64 and the scalar loops elsewhere;
// both give the same bits. True runs the scalar loops on x64 too, so a test can compare the two. Main thread only,
// between steps.
void set_fluid_scalar_kernels_2d(bool scalar);

// Runs the solver's spatial-hash neighbour search (every other particle closer than `h`) on `positions`.
[[nodiscard]] FluidNeighbors2D fluid_neighbors_2d(std::span<const glm::vec2> positions, float h, JobSystem* jobs);

}
