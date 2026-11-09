#pragma once

// docs/tech/modules/ECS.md (2D fluids)

#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {

class JobSystem;
struct Fluid2DSolver;

// The tuning of a Fluid2D, serialized as `fluid_2d`.
struct Fluid2DParams {
    // Particle radius in world units: the distance kept from every boundary, and half the rest spacing of
    // `spawn_box`. Not positive or non-finite: 0.05.
    float radius = 0.05f;
    // Kernel support h, in world units. Not positive or non-finite: 3 rest spacings (6 radii), about 24 neighbours
    // per particle at rest.
    float smoothing_length = 0.0f;
    // Rest density in the solver's normalized units (unit mass, h = 1). Not positive or non-finite: the density of a
    // square lattice at the rest spacing, so `spawn_box` starts at rest.
    float rest_density = 0.0f;
    // XSPH viscosity c in [0, 1]: each substep blends a particle's velocity toward its neighbours' (weighted by the
    // kernel over rest density). 0 is inviscid; below about 0.05 a resting pool keeps jittering.
    float viscosity = 0.1f;
    // Constraint relaxation (the CFM epsilon of the paper, normalized by h): larger is softer and more stable.
    float relaxation = 0.5f;
    // Tensile correction (s_corr, n = 4, dq = 0.2 h) strength k in the normalized units; 0 turns it off. A weak
    // repulsion at the rest spacing that keeps particles from pairing and lets a resting pool come to rest; about
    // 0.01 and above it visibly expands the fluid. The paper's 0.1 is for its units, not these.
    float tensile_strength = 0.005f;
    // Density solver iterations per substep, clamped to [1, 32].
    int iterations = 4;
    // Substeps per fixed tick, clamped to [1, 16]. Each substep searches neighbours once.
    int substeps = 2;
    // `spawn` refuses particles beyond this count.
    std::size_t max_particles = 16384;
    // Multiplies `Physics2DConfig::gravity` (its XY).
    float gravity_scale = 1.0f;
    // Fraction of the tangential velocity removed per substep in contact with a boundary, clamped to [0, 1].
    float friction = 0.1f;
    // Fraction of the normal approach speed kept, reflected, after a boundary contact, clamped to [0, 1].
    float restitution = 0.0f;
    // Mass per unit area in world units, the unit of `RigidBody2D::mass` over a collider's area: a Dynamic body lighter
    // than that per area floats, a heavier one sinks. A particle weighs `density` times its lattice cell, (2 radius)^2.
    // 0 (or negative, or non-finite) turns the reaction on bodies off: coupling one way again.
    float density = 1.0f;
    // Cohesion (Akinci et al. 2013, the cohesion term): pulls particles toward their neighbours at the free surface,
    // a surface tension that holds a blob together as a drop and rounds it. 0 is off; see docs/manual/ecs/Fluids.md
    // for the tested range.
    float cohesion = 0.0f;
    // Vorticity confinement (Macklin and Mueller 2013) strength: puts back the swirl the solver and viscosity damp,
    // so eddies live longer. 0 is off; see docs/manual/ecs/Fluids.md for the tested range.
    float vorticity = 0.0f;
    uint32_t layer = 1;           // what the fluid is, for the colliders' masks
    uint32_t mask = 0xFFFFFFFFu;  // which collider layers bound it
};

// A region of world XY for fluid sources, drains, sensors and queries: an axis-aligned box or a circle.
struct FluidRegion2D {
    enum class Shape : std::uint8_t { Box, Circle };
    Shape shape = Shape::Box;
    glm::vec2 center{0.0f};
    glm::vec2 half_extents{0.5f};  // Box
    float radius = 0.5f;           // Circle

    // Inside or on the edge. A negative size holds nothing.
    [[nodiscard]] bool contains(glm::vec2 point) const;
    // World units squared; 0 for a negative or non-finite size.
    [[nodiscard]] float area() const;
};

// Emits `rate` particles per second inside `region`, each with `velocity`, before the fluid steps. The fraction of a
// particle a tick does not emit is carried to the next. Positions follow a low-discrepancy sequence over the region
// (the same on every peer), so a stream spreads evenly without random numbers. Emission stops at `max_particles`;
// what does not fit is dropped, not queued.
struct FluidSource2D {
    FluidRegion2D region{};
    float rate = 0.0f;  // particles per second; not positive or non-finite: none
    glm::vec2 velocity{0.0f};
    bool enabled = true;
    // State, not serialized: the part of a particle not emitted yet, and how many this source has emitted.
    float carry = 0.0f;
    std::uint64_t emitted = 0;
};

// Removes every particle that ends a step inside `region`. The others keep their order.
struct FluidDrain2D {
    FluidRegion2D region{};
    bool enabled = true;
    // State, not serialized: particles removed so far.
    std::uint64_t drained = 0;
};

// Watches how full `region` is after each step: the particles inside times their lattice cell, (2 radius)^2, over the
// region's area (about 1 for a region under water at rest). When the fill reaches `threshold` it is `full` and a
// FluidSensorEvent2D with `full = true` is sent; it stops being full (and sends `full = false`) only below
// `threshold - hysteresis`, so a level wobbling at the threshold does not send an event every tick.
struct FluidSensor2D {
    FluidRegion2D region{};
    float threshold = 0.5f;
    float hysteresis = 0.05f;
    // State, not serialized: the last fill and whether the region counts as full.
    float fill = 0.0f;
    bool full = false;
};

// Sent (double-buffered, `ecs::EventReader<FluidSensorEvent2D>`) when a sensor of `fluid` turns full or stops being
// full. `sensor` indexes `Fluid2D::sensors`.
struct FluidSensorEvent2D {
    ecs::Entity fluid{};
    std::uint32_t sensor = 0;
    bool full = false;
    float fill = 0.0f;
    std::size_t count = 0;  // particles inside

    constexpr bool operator==(const FluidSensorEvent2D&) const noexcept = default;
};

// What a fluid query found: how many particles, the area they stand for (count times (2 radius)^2), and their mean
// velocity (zero when none).
struct FluidQuery2D {
    std::size_t count = 0;
    float volume = 0.0f;
    glm::vec2 average_velocity{0.0f};
};

// A 2D liquid: Position Based Fluids (Macklin and Mueller 2013) particles in world XY, stepped by `run_fluid_2d` on
// Fixed / Physics after `run_physics_2d` and `run_physics_3d`, on a world `install_fluid_2d` ran on. Without the
// install a Fluid2D keeps its particles and never moves. `params` is plain data (serialized as `fluid_2d`); the
// particles are owned state behind `spawn`, `spawn_box` and `clear`, transient (not serialized, not replicated).
// The entity needs no Transform: positions are world XY.
//
// The fluid is pushed out of non-trigger `SdfCollider2D`, `BoxCollider2D`, `CircleCollider2D`,
// `PolygonCollider2D` and `CompoundCollider2D` colliders that pass the layer rule (`mask & collider.layer` and
// `collider.mask & layer`), static, kinematic or dynamic, at their pose after `run_physics_2d`. An `SdfCollider2D` or
// `CompoundCollider2D` under a Dynamic body is skipped, as the 2D solver skips it. Coupling is two-way for a box,
// circle or polygon collider on a Dynamic body's own entity: the fluid pushes it back (buoyancy, drag) with the
// reaction of every push, added to its velocity and angular velocity at the end of the step for the next
// `run_physics_2d` to integrate. Everything else only pushes. No `CollisionEvent2D`. Two fluids do not see each other.
//
// Rendering is the game's: draw `positions()` with its own sprites or particles.
class Fluid2D {
public:
    Fluid2D() = default;
    explicit Fluid2D(const Fluid2DParams& tuning) : params(tuning) {}

    Fluid2DParams params{};
    // Taps, sinks and level sensors, stepped by `run_fluid_2d` in that order around the solver step: sources emit, the
    // fluid steps, drains remove, sensors measure. Their settings are serialized with `params`, their state is not.
    std::vector<FluidSource2D> sources;
    std::vector<FluidDrain2D> drains;
    std::vector<FluidSensor2D> sensors;

    // Adds one particle at world `position` with `velocity`. False (nothing added) at `max_particles` or for a
    // non-finite input.
    bool spawn(glm::vec2 position, glm::vec2 velocity = glm::vec2{0.0f});
    // Fills the rectangle [min, max] with particles on a square lattice at the rest spacing (2 radii), the first one
    // a radius in from `min`, row by row from the bottom. Stops at `max_particles`. Returns how many were added.
    std::size_t spawn_box(glm::vec2 min, glm::vec2 max, glm::vec2 velocity = glm::vec2{0.0f});
    // Removes every particle. Parameters are kept.
    void clear();

    // --- Queries, on the particles as the last step (or spawn) left them. The first query after a change builds a
    // lookup grid of cells two radii wide (one pass over the particles); later queries read only the cells they touch.
    // Main thread, like every engine call.
    [[nodiscard]] FluidQuery2D query(const FluidRegion2D& region) const;
    // Inside a simple polygon in world XY (either winding, convex or not; the even-odd rule). Fewer than 3 vertices
    // or a non-finite one: nothing.
    [[nodiscard]] FluidQuery2D query(std::span<const glm::vec2> polygon) const;
    // A particle center within the rest spacing (2 radii) of `point`.
    [[nodiscard]] bool is_wet(glm::vec2 point) const;

    [[nodiscard]] std::size_t count() const {
        return positions_.size();
    }
    // World XY, in spawn order; the order never changes between steps.
    [[nodiscard]] std::span<const glm::vec2> positions() const {
        return positions_;
    }
    [[nodiscard]] std::span<const glm::vec2> velocities() const {
        return velocities_;
    }
    // Density over rest density (1 at rest, below 1 at the free surface) from the last solver iteration of the last
    // step; zero for a particle spawned since. For shading foam or pressure.
    [[nodiscard]] std::span<const float> densities() const {
        return densities_;
    }
    // FNV-1a 64 of the simulation state: the particle count, then each particle's position and velocity bits in
    // spawn order, then each source's carry and emitted count and each sensor's `full`. Equal on two peers exactly
    // when their fluids are the same bits (desync detection, golden tests). Parameters, drain counts, sensor fills and
    // densities (outputs of the last step) are not in it.
    [[nodiscard]] std::uint64_t state_hash() const;

private:
    friend struct Fluid2DSolver;

    void build_query_grid() const;
    template <class F>
    void for_each_in_cells(glm::vec2 lo, glm::vec2 hi, F&& fn) const;

    std::vector<glm::vec2> positions_;
    std::vector<glm::vec2> velocities_;
    std::vector<float> densities_;
    // The queries' lookup grid: a counting sort of the particles by cell (two radii) into a power-of-two hash table.
    // Rebuilt on the first query after the particles changed.
    mutable std::vector<std::uint32_t> query_bucket_start_;
    mutable std::vector<std::uint32_t> query_sorted_;
    mutable std::vector<glm::ivec2> query_cell_;
    mutable bool query_valid_ = false;
};

// Fluids are opt-in: the engine does not step them unless the game installs them. Registers `run_fluid_2d` on
// Fixed / Physics once, after the engine's `run_physics_2d` and `run_physics_3d`. Called before the engine registers
// its simulation systems (a game's constructor), the system is added right after `run_physics_3d`; called later
// (`on_start`), it is appended to the phase, after any Physics system the game added before it. A second call
// replaces `jobs` without adding the system again. Pass `EngineServices::jobs` for parallel passes.
void install_fluid_2d(ecs::World& world, JobSystem* jobs = nullptr);

// Steps every Fluid2D by `Time::fixed_delta_time`: the system `install_fluid_2d` registers. Do not register it
// yourself; call it directly only on a world without that install (a bench, a tool). With `jobs`, per-particle
// passes run in `parallel_for`; every pass is a gather (a particle writes only its own slots), so the result is
// bit-identical for any worker count.
void run_fluid_2d(ecs::World& world, JobSystem* jobs = nullptr);

}
