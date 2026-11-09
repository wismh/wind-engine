#pragma once

// docs/tech/modules/Render.md

#include <engine/ecs/entity.h>
#include <engine/render/commands.h>
#include <engine/render/curve.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/ext/vector_int3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine {
class JobSystem;
class SdfField2D;
}

namespace engine::render {

// A 2D collider copied for particles, XY only. Box and Circle are axis-aligned shapes at `position` (box yaw is
// ignored). Sdf is a non-owning view of an `SdfField2D` (an `SdfCollider2D`) whose field-local XY is placed at
// `position.xy` and turned by `angle` (radians about +Z); scale is not applied, as in the 2D solver. `field` must stay
// alive and unedited through the `update_emitter` call that reads it: `run_particles` points it into the component
// pool for that call only. A null `field` is skipped.
struct ParticleCollider2D {
    enum class Shape {
        Box,
        Circle,
        Sdf,
    };

    Shape shape = Shape::Box;
    glm::vec3 position{0.0f};
    glm::vec3 box_size{1.0f, 1.0f, 1.0f};
    float circle_radius = 0.5f;
    uint32_t layer = 1;
    // Sdf only.
    const SdfField2D* field = nullptr;
    float angle = 0.0f;
};

// Box, sphere, and capsule copied from the non-trigger 3D colliders. A box is oriented: `rotation` is the world
// quaternion (the Transform chain). Scale is not applied. Sphere ignores `rotation`.
struct ParticleCollider3D {
    enum class Shape {
        Box,
        Sphere,
        Capsule,
    };

    Shape shape = Shape::Box;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 box_size{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    uint32_t layer = 1;
};

// World context the game fills. `data` stays alive through `run_particles`. `is_solid` runs on the caller after the
// integrate job joins. A null function does nothing. This is not a `CellPredicate` and is not kept across frames
// as one: the sweep builds that predicate on the stack.
struct ParticleGrid {
    bool (*is_solid)(const void* data, glm::ivec3 cell) = nullptr;
    const void* data = nullptr;
};

// Sphere is the solid ellipsoid of `shape_scale` (semi-axes). Disc is the `shape_scale.xy` ellipse in the plane
// of `direction`, or +Y when that vector is near zero. Circle stays an XY disc with z = 0.
enum class EmitterShape {
    Point,
    Box,
    Circle,
    Cone,
    Sphere,
    Disc,
};

// How one particle is drawn. Plane is the 2D quad. 0 so a value-initialized mode stays Plane.
enum class ParticleDraw : std::uint8_t {
    Plane = 0,
    Billboard = 1,
    Stretched = 2,
    Mesh = 3,
};

static_assert(static_cast<std::uint8_t>(ParticleDraw::Plane) == 0);
static_assert(static_cast<std::uint8_t>(ParticleDraw::Mesh) == 3);

// True only for Mesh. The card shader never sees that mode, and only Mesh sets uInstanced.
[[nodiscard]] constexpr bool particle_draw_uses_instance_transform(ParticleDraw draw) noexcept {
    return draw == ParticleDraw::Mesh;
}

// A camera row shorter than this is replaced, not normalized.
inline constexpr float kParticleAxisEpsilon = 1e-8f;
// In-plane speed at or below this uses Billboard. Straight at the camera counts as no in-plane speed.
inline constexpr float kParticleStretchMinSpeed = 1e-4f;

enum class SimulationSpace {
    World,
    Local,
};

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 size{1.0f, 1.0f};
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    glm::vec2 base_size{1.0f, 1.0f};
    glm::vec2 start_size{1.0f, 1.0f};
    glm::vec2 end_size{0.0f, 0.0f};
    glm::vec4 start_color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 end_color{1.0f, 1.0f, 1.0f, 0.0f};
    // Copied from the emitter at spawn. Array mesh shaders sample it. sampler2D ignores it.
    float texture_layer = 0.0f;
};

struct ParticleEmitter {
    // Emission parameters
    float emission_rate = 10.0f;          // particles spawned per second
    std::size_t max_particles = 1000;     // maximum capacity of active particles
    float duration = 0.0f;                // duration in seconds (<= 0 means infinite loop)
    bool looping = true;
    bool playing = true;

    // Lifetime & Speed
    float lifetime_min = 1.0f;
    float lifetime_max = 1.0f;
    float speed_min = 1.0f;
    float speed_max = 1.0f;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread_angle = 0.0f;            // spread half-angle in radians for direction/cone

    // Size & Color over lifetime
    glm::vec2 size_start_min{1.0f, 1.0f};
    glm::vec2 size_start_max{1.0f, 1.0f};
    glm::vec2 size_end_min{0.0f, 0.0f};
    glm::vec2 size_end_max{0.0f, 0.0f};
    glm::vec4 color_start{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 color_end{1.0f, 1.0f, 1.0f, 0.0f};

    // Custom animation curves (evaluated over normalized lifetime t in [0, 1]).
    // When non-empty, these curves override the default linear start/end values.
    Curve<float> size_curve;          // Uniform scale multiplier over lifetime
    Curve<glm::vec2> size_curve_xy;   // Non-uniform XY scale multiplier over lifetime
    Curve<glm::vec4> color_curve;     // Color gradient over lifetime
    Curve<float> alpha_curve;         // Opacity / alpha curve over lifetime

    // Rotation (radians)
    float rotation_min = 0.0f;
    float rotation_max = 0.0f;
    float angular_velocity_min = 0.0f;
    float angular_velocity_max = 0.0f;

    // Physics forces & space
    glm::vec3 gravity{0.0f, 0.0f, 0.0f};
    EmitterShape shape = EmitterShape::Point;
    glm::vec3 shape_scale{1.0f, 1.0f, 1.0f};
    SimulationSpace simulation_space = SimulationSpace::World;

    // Collision parameters
    bool collision_enabled = false;
    // With a `ParticleGrid` context, sweep solid cells after integrate. No context does nothing.
    bool collision_grid = false;
    uint32_t collision_mask = 0xFFFFFFFFu; // bitmask of layers to collide with
    float bounce = 0.5f;                  // coefficient of restitution [0, 1]
    float friction = 0.0f;                // tangential velocity damping [0, 1]
    float lifetime_loss = 0.0f;           // fraction of lifetime lost on hit [0, 1]
    bool kill_on_collision = false;       // immediately kill particle on contact
    float collision_radius = 0.0f;        // 0.0f treats particle as a point; >0 gives thickness

    // Rendering configuration
    // Plane keeps the world-XY quad. Billboard and Stretched face the camera. Mesh instances `mesh`.
    ParticleDraw draw = ParticleDraw::Plane;
    // False draws the emitter even when its particles miss every window frustum. A card shader that
    // moves vertices past the CPU box needs this. Not a `.world` key: the emitter is not serialized.
    bool frustum_cull = true;
    // Mesh only. `size_curve` multiplies it when that curve has keys. An empty curve leaves it unchanged.
    // `size` and `size_curve_xy` do not. Cards ignore it.
    glm::vec3 mesh_scale{1.0f};
    // Copied onto each particle at spawn. Default 0.
    float texture_layer = 0.0f;
    std::shared_ptr<ITexture> texture;
    std::shared_ptr<IMaterial> material;
    std::shared_ptr<IMesh> mesh;
    BlendMode blend = BlendMode::Alpha;
    int layer = 0;
    int order_in_layer = 0;
    glm::vec2 uv_scale{1.0f, 1.0f};
    glm::vec2 uv_offset{0.0f, 0.0f};

    // Runtime state
    std::vector<Particle> particles;
    float emission_accumulator = 0.0f;
    float elapsed_time = 0.0f;
    int pending_burst = 0;

    void play() noexcept {
        playing = true;
        elapsed_time = 0.0f;
    }

    void pause() noexcept {
        playing = false;
    }

    void stop() noexcept {
        playing = false;
        elapsed_time = 0.0f;
        emission_accumulator = 0.0f;
        pending_burst = 0;
        particles.clear();
    }

    void burst(int count) noexcept {
        if (count > 0) {
            pending_burst += count;
        }
    }

    [[nodiscard]] std::size_t active_count() const noexcept {
        return particles.size();
    }
};

// World offset of one unit-quad corner (`corner` is ±0.5). Plane is world XY and ignores the camera.
// Billboard and Stretched lie in the plane of camera right and up (the first two rows of the view).
// Stretched points local +Y along the in-plane part of `velocity`.
// 1 when `size_curve` has no keys. Otherwise `size_curve.evaluate` at `clamp(age / lifetime, 0, 1)`.
// `lifetime <= 0` evaluates the curve at 0. Does not read `Particle::size` or `size_curve_xy`.
[[nodiscard]] float particle_mesh_size_factor(const Curve<float>& size_curve, float age, float lifetime) noexcept;

// Position, `mesh_scale * size_factor`, color, and layer. Orientation is identity.
[[nodiscard]] MeshParticleInstance particle_mesh_instance(
        glm::vec3 position, glm::vec3 mesh_scale, float size_factor, glm::vec4 color, float layer) noexcept;

[[nodiscard]] glm::vec3 particle_card_offset(
        ParticleDraw draw,
        glm::vec3 camera_right,
        glm::vec3 camera_up,
        glm::vec2 corner,
        glm::vec2 size,
        float rotation,
        glm::vec3 velocity) noexcept;

// `jobs` null integrates on the caller. Spawn stays on the caller either way. Dead particles are dropped
// after the integrate joins, and the survivors keep their order. Both collider spans, and the fields of Sdf
// colliders (read-only), are read inside the job; the result does not depend on the worker count. Per particle and
// step: the swept box, circle and 3D tests first, then each Sdf collider in span order, then `grid` on the caller.
// `grid` is swept on the caller after that join. A null grid, or a null `is_solid`, does nothing.
void update_emitter(
        ParticleEmitter& emitter,
        float dt,
        const glm::mat4& transform = glm::mat4{1.0f},
        std::span<const ParticleCollider2D> colliders = {},
        std::span<const ParticleCollider3D> colliders_3d = {},
        JobSystem* jobs = nullptr,
        const ParticleGrid* grid = nullptr);

}
