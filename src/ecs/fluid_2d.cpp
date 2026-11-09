#include <engine/ecs/fluid_2d.h>

#include <engine/core/det_math.h>
#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>

#include "core/state_hash.h"
#include "fluid_2d_internal.h"
#include "inertia_2d.h"
#include "physics_2d_internal.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// docs/tech/modules/ECS.md (2D fluids). Position Based Fluids, Macklin and Mueller 2013, in 2D.
//
// The solver works in units of the kernel support: every particle offset is multiplied by 1 / h, so the kernels are
// the h = 1 forms and the tuning constants (rest density, relaxation, s_corr) do not depend on the world scale. A
// position delta is multiplied by h on the way back.

namespace engine {
namespace {

constexpr float kPi = 3.14159265358979f;
// 2D poly6, W(r) = 4 / (pi h^8) (h^2 - r^2)^3, at h = 1.
constexpr float kPoly6 = 4.0f / kPi;
// 2D spiky gradient, grad W(r) = -30 / (pi h^5) (h - r)^2 r_hat, at h = 1.
constexpr float kSpikyGrad = -30.0f / kPi;
// Akinci et al. 2013 cohesion spline C(q) at h = 1, its 32 / (pi h^9) factor: largest, about 0.16, at q = 0.5.
constexpr float kCohesionSpline = 32.0f / kPi;
// s_corr reference distance dq (in h) and exponent n of the paper.
constexpr float kTensileDq = 0.2f;
// Neighbours kept per particle, in cell order. About 24 at rest; the cap only bites under heavy compression.
constexpr std::size_t kMaxNeighbors = kFluidMaxNeighbors2D;
// Particles per parallel_for range.
constexpr std::size_t kGrain = 256;
// Particles per SDF `sample_many` batch (stack arrays).
constexpr std::size_t kSdfBatch = 64;
// A solver delta is at most this many radii per iteration: a deep overlap (a spawn inside another particle, a
// collider teleported into the fluid) spreads over several iterations instead of exploding.
constexpr float kMaxDeltaRadii = 1.0f;
// A particle that moved farther than this many radii in an iteration (the first one includes the predicted step) is
// swept against the boundaries before it is projected, so a fast one cannot cross a wall thinner than its step. A
// shorter step from a resting distance of one radius cannot reach past the middle of a wall half a radius thick, and
// resting fluid pays one length test.
constexpr float kSweepRadii = 0.5f;
// A swept particle stops this many radii off the surface it touched.
constexpr float kSweepSkinRadii = 0.01f;
// Dynamic bodies a particle can push back in one substep (two-way coupling); a third one adds to the second slot.
constexpr std::size_t kCouplingSlots = 2;

[[nodiscard]] float poly6(float r2) {
    if (r2 >= 1.0f) {
        return 0.0f;
    }
    const float t = 1.0f - r2;
    return kPoly6 * t * t * t;
}

// `d` = x_i - x_j in h units, `r` = |d|.
[[nodiscard]] glm::vec2 spiky_grad(glm::vec2 d, float r) {
    if (r <= 1.0e-6f || r >= 1.0f) {
        return glm::vec2{0.0f};
    }
    const float t = 1.0f - r;
    return d * (kSpikyGrad * t * t / r);
}

// Akinci's cohesion spline C(q), q = r / h in (0, 1]: attraction beyond q = 0.5, a slight repulsion closer.
[[nodiscard]] float cohesion_spline(float q) {
    if (!(q > 0.0f) || q >= 1.0f) {
        return 0.0f;
    }
    const float a = (1.0f - q) * (1.0f - q) * (1.0f - q) * q * q * q;
    return kCohesionSpline * (q > 0.5f ? a : 2.0f * a - 1.0f / 64.0f);
}

[[nodiscard]] bool finite(glm::vec2 v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

[[nodiscard]] float positive_or(float value, float fallback) {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
}

[[nodiscard]] float clamp01(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

[[nodiscard]] float particle_radius(const Fluid2DParams& params) {
    return positive_or(params.radius, 0.05f);
}

// The nearest RigidBody2D on the entity or above it is Dynamic.
[[nodiscard]] bool under_dynamic_body(const ecs::World& world, ecs::Entity entity) {
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
        if (const RigidBody2D* body = world.try_get<RigidBody2D>(current)) {
            return body->type == BodyType::Dynamic;
        }
        const Parent* parent = world.try_get<Parent>(current);
        if (parent == nullptr || !world.valid(parent->entity)) {
            return false;
        }
        current = parent->entity;
    }
    return false;
}

// The nearest RigidBody2D on the entity or above it is Dynamic or Kinematic: the 2D solver's movable collider, never in
// its static cache.
[[nodiscard]] bool under_movable_body(const ecs::World& world, ecs::Entity entity) {
    ecs::Entity current = entity;
    for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
        if (const RigidBody2D* body = world.try_get<RigidBody2D>(current)) {
            return body->type == BodyType::Dynamic || body->type == BodyType::Kinematic;
        }
        const Parent* parent = world.try_get<Parent>(current);
        if (parent == nullptr || !world.valid(parent->entity)) {
            return false;
        }
        current = parent->entity;
    }
    return false;
}

[[nodiscard]] bool layers_match(const Fluid2DParams& fluid, uint32_t layer, uint32_t mask) {
    return (fluid.mask & layer) != 0 && (mask & fluid.layer) != 0;
}

// A collider pose: local XY = R(-angle) * (world XY - position).
struct Frame2D {
    glm::vec2 position{0.0f};
    float cos_a = 1.0f;
    float sin_a = 0.0f;

    [[nodiscard]] glm::vec2 rotate_to_local(glm::vec2 v) const {
        return {cos_a * v.x + sin_a * v.y, -sin_a * v.x + cos_a * v.y};
    }
    [[nodiscard]] glm::vec2 rotate_to_world(glm::vec2 v) const {
        return {cos_a * v.x - sin_a * v.y, sin_a * v.x + cos_a * v.y};
    }
    [[nodiscard]] glm::vec2 to_local(glm::vec2 world) const {
        return rotate_to_local(world - position);
    }
    [[nodiscard]] glm::vec2 to_world(glm::vec2 local) const {
        return position + rotate_to_world(local);
    }
};

// The heading comes from the matrix X axis in XY itself, normalized (scale changes its length only), with the
// solver's epsilon: no angle and no trigonometry on the way.
[[nodiscard]] Frame2D frame_of(const glm::mat4& matrix) {
    const glm::vec2 position{matrix[3][0], matrix[3][1]};
    const float x = matrix[0][0];
    const float y = matrix[0][1];
    if (std::abs(x) < 1.0e-6f && std::abs(y) < 1.0e-6f) {
        return Frame2D{.position = position, .cos_a = 1.0f, .sin_a = 0.0f};
    }
    const float length = std::sqrt(x * x + y * y);
    return Frame2D{.position = position, .cos_a = x / length, .sin_a = y / length};
}

struct SdfBoundary {
    const SdfField2D* field = nullptr;
    Frame2D frame{};
    uint32_t layer = 0;
    uint32_t mask = 0;
};

struct ShapeBoundary {
    enum class Kind { Box, Circle, Polygon };
    Kind kind = Kind::Box;
    Frame2D frame{};
    glm::vec2 half{0.0f};  // Box
    float radius = 0.0f;   // Circle
    // Polygon: convex, counter-clockwise, in world space, with each edge's outward unit normal (edge i runs from
    // vertex i to vertex i + 1).
    std::array<glm::vec2, kPolygonCollider2DCapacity> vertices{};
    std::array<glm::vec2, kPolygonCollider2DCapacity> normals{};
    int count = 0;
    uint32_t layer = 0;
    uint32_t mask = 0;
    // Index into `Boundaries::bodies` when the shape's own entity is a Dynamic RigidBody2D (two-way coupling), else -1.
    int body = -1;
    // World AABB of the shape.
    glm::vec2 lo{0.0f};
    glm::vec2 hi{0.0f};
};

// Every collider that can bound a fluid this tick, in view order. `field` points into the SdfCollider2D pool for this
// call only: run_fluid_2d creates and destroys nothing.
// A Dynamic body a fluid pushes back: the reaction impulses of every substep, summed in particle order, become a change
// of its velocity at the end of run_fluid_2d, which the next run_physics_2d integrates.
struct CouplingBody2D {
    ecs::Entity entity{};
    glm::vec2 origin{0.0f};  // world position of the body: the 2D solver's center of rotation
    glm::vec2 impulse{0.0f};
    float angular_impulse = 0.0f;
    // The fastest particle that pushed it this tick (after its substep), and the longest contact arm: the change of
    // velocity is capped by them (apply_coupling).
    float particle_speed = 0.0f;
    float arm = 0.0f;
};

struct Boundaries {
    std::vector<SdfBoundary> sdf;
    std::vector<ShapeBoundary> shapes;
    std::vector<CouplingBody2D> bodies;
};

// The Dynamic RigidBody2D on `entity` itself, or null. Only a collider on the body's own entity is coupled, as only
// such a collider gives the 2D solver the body's inertia.
[[nodiscard]] const RigidBody2D* dynamic_body(const ecs::World& world, ecs::Entity entity) {
    const RigidBody2D* body = world.try_get<RigidBody2D>(entity);
    return body != nullptr && body->type == BodyType::Dynamic ? body : nullptr;
}

// The coupling index of `entity` in `out.bodies`, added on first use. Bodies keep the order of their first collider.
[[nodiscard]] int coupling_index(Boundaries& out, ecs::Entity entity, glm::vec2 origin) {
    for (std::size_t i = 0; i < out.bodies.size(); ++i) {
        if (out.bodies[i].entity == entity) {
            return static_cast<int>(i);
        }
    }
    out.bodies.push_back(CouplingBody2D{.entity = entity, .origin = origin});
    return static_cast<int>(out.bodies.size()) - 1;
}

// A polygon boundary from the 2D solver's item for a PolygonCollider2D or a CompoundCollider2D part, at the item's
// pose (the solver's own vertices: `contact_shape_of_2d`). False for a polygon the solver's rules reject: fewer than 3
// vertices, a non-finite one, a zero-length edge, or a clockwise turn.
[[nodiscard]] bool polygon_boundary(const ColliderItem2D& item, ShapeBoundary& out) {
    if (item.is_trigger || item.polygon_count < 3 || item.polygon_count > kPolygonCollider2DCapacity) {
        return false;
    }
    const ContactShape2D shape = contact_shape_of_2d(item);
    const int count = std::min(shape.vertex_count, kPolygonCollider2DCapacity);
    if (count < 3) {
        return false;
    }
    out = ShapeBoundary{.kind = ShapeBoundary::Kind::Polygon, .layer = item.layer, .mask = item.mask};
    out.count = count;
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{-std::numeric_limits<float>::max()};
    for (int i = 0; i < count; ++i) {
        const glm::vec2 v = shape.vertices[i];
        if (!finite(v)) {
            return false;
        }
        out.vertices[static_cast<std::size_t>(i)] = v;
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
    for (int i = 0; i < count; ++i) {
        const glm::vec2 a = out.vertices[static_cast<std::size_t>(i)];
        const glm::vec2 b = out.vertices[static_cast<std::size_t>((i + 1) % count)];
        const glm::vec2 c = out.vertices[static_cast<std::size_t>((i + 2) % count)];
        const glm::vec2 edge = b - a;
        const float length = std::sqrt(glm::dot(edge, edge));
        const glm::vec2 next = c - b;
        if (!(length > 0.0f) || edge.x * next.y - edge.y * next.x < 0.0f) {
            return false;
        }
        out.normals[static_cast<std::size_t>(i)] = glm::vec2{edge.y, -edge.x} / length;
    }
    out.lo = lo;
    out.hi = hi;
    return true;
}

// Every collider that can bound a fluid this tick. `reach` is where any fluid can be this tick (world XY): the static
// polygon and compound parts are read from the 2D solver's static AABB tree within it, when that cache is up to date
// (run_physics_2d ran this tick), so the cost follows the terrain near the fluid, not the whole map. Without it (a
// world that does not run run_physics_2d) they come from the component views like everything else.
Boundaries gather_boundaries(ecs::World& world, glm::vec2 reach_lo, glm::vec2 reach_hi) {
    Boundaries out;
    world.view<Transform, SdfCollider2D>().each([&](ecs::Entity entity, const Transform&, const SdfCollider2D& sdf) {
        if (under_dynamic_body(world, entity)) {
            return;
        }
        out.sdf.push_back(SdfBoundary{
                .field = &sdf.field,
                .frame = frame_of(ecs::world_matrix(world, entity)),
                .layer = sdf.layer,
                .mask = sdf.mask,
        });
    });
    world.view<Transform, BoxCollider2D>().each([&](ecs::Entity entity, const Transform&, const BoxCollider2D& box) {
        const glm::vec2 half{std::abs(box.size.x) * 0.5f, std::abs(box.size.y) * 0.5f};
        if (box.is_trigger || !finite(half)) {
            return;
        }
        ShapeBoundary shape{
                .kind = ShapeBoundary::Kind::Box,
                .frame = frame_of(ecs::world_matrix(world, entity)),
                .half = half,
                .layer = box.layer,
                .mask = box.mask,
        };
        const glm::vec2 extent{std::abs(shape.frame.cos_a) * half.x + std::abs(shape.frame.sin_a) * half.y,
                               std::abs(shape.frame.sin_a) * half.x + std::abs(shape.frame.cos_a) * half.y};
        shape.lo = shape.frame.position - extent;
        shape.hi = shape.frame.position + extent;
        if (dynamic_body(world, entity) != nullptr) {
            shape.body = coupling_index(out, entity, shape.frame.position);
        }
        out.shapes.push_back(shape);
    });
    world.view<Transform, CircleCollider2D>().each(
            [&](ecs::Entity entity, const Transform&, const CircleCollider2D& circle) {
        if (circle.is_trigger || !std::isfinite(circle.radius) || circle.radius <= 0.0f) {
            return;
        }
        ShapeBoundary shape{
                .kind = ShapeBoundary::Kind::Circle,
                .frame = frame_of(ecs::world_matrix(world, entity)),
                .radius = circle.radius,
                .layer = circle.layer,
                .mask = circle.mask,
        };
        shape.lo = shape.frame.position - glm::vec2{circle.radius};
        shape.hi = shape.frame.position + glm::vec2{circle.radius};
        if (dynamic_body(world, entity) != nullptr) {
            shape.body = coupling_index(out, entity, shape.frame.position);
        }
        out.shapes.push_back(shape);
    });

    const PhysicsBroadphase2D* cache = world.try_ctx<PhysicsBroadphase2D>();
    const bool from_cache = cache != nullptr && !cache->dirty && cache->marked_entities.empty();
    world.view<Transform, PolygonCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const PolygonCollider2D& polygon) {
        const bool movable = under_movable_body(world, entity);
        if (from_cache && !movable) {
            return;  // in the static cache, read below
        }
        ShapeBoundary shape;
        const glm::mat4 matrix = collider_world_matrix_2d(world, entity, transform);
        if (polygon_boundary(make_polygon_item_2d(entity, matrix, polygon, movable), shape)) {
            if (dynamic_body(world, entity) != nullptr) {
                shape.body = coupling_index(out, entity, glm::vec2{matrix[3][0], matrix[3][1]});
            }
            out.shapes.push_back(shape);
        }
    });
    world.view<Transform, CompoundCollider2D>().each(
            [&](ecs::Entity entity, const Transform& transform, const CompoundCollider2D& compound) {
        // Under a Dynamic body the solver ignores a compound; so does the fluid.
        const bool movable = under_movable_body(world, entity);
        if ((from_cache && !movable) || under_dynamic_body(world, entity)) {
            return;
        }
        const glm::mat4 matrix = collider_world_matrix_2d(world, entity, transform);
        for (std::uint32_t part = 0; part < compound.shapes.size(); ++part) {
            ShapeBoundary shape;
            if (polygon_boundary(make_compound_item_2d(entity, matrix, compound, part, movable), shape)) {
                out.shapes.push_back(shape);
            }
        }
    });
    if (from_cache) {
        // Static polygons and compound parts near the fluid, in (entity, shape, part) order.
        std::vector<const ColliderItem2D*> items;
        const Aabb box{glm::vec3{reach_lo, -1.0f}, glm::vec3{reach_hi, 1.0f}};
        cache->static_tree.query(box, [&](int leaf) {
            const PhysicsBroadphase2D::Slot& slot = cache->static_slots[static_cast<std::size_t>(leaf)];
            if (slot.live &&
                (slot.item.shape == ColliderShape2D::Polygon || slot.item.shape == ColliderShape2D::Compound)) {
                items.push_back(&slot.item);
            }
        });
        std::sort(items.begin(), items.end(), [](const ColliderItem2D* a, const ColliderItem2D* b) {
            return collider_order_less_2d(*a, *b);
        });
        for (const ColliderItem2D* item : items) {
            ShapeBoundary shape;
            if (polygon_boundary(*item, shape)) {
                out.shapes.push_back(shape);
            }
        }
    }
    return out;
}

template <class F>
void for_ranges(JobSystem* jobs, std::size_t count, F&& fn) {
    if (count == 0) {
        return;
    }
    if (jobs == nullptr || count <= kGrain) {
        fn(std::size_t{0}, count);
        return;
    }
    jobs->parallel_for(count, kGrain, fn);
}

// A hash cell coordinate, clamped so a far or non-finite position cannot overflow the cast.
[[nodiscard]] int cell_coord(float v) {
    return std::isfinite(v) ? static_cast<int>(std::clamp(std::floor(v), -1.0e9f, 1.0e9f)) : 0;
}

[[nodiscard]] std::uint32_t cell_hash(glm::ivec2 cell, std::uint32_t mask) {
    const auto x = static_cast<std::uint32_t>(cell.x) * 73856093u;
    const auto y = static_cast<std::uint32_t>(cell.y) * 19349663u;
    return (x ^ y) & mask;
}

// The solver's resolved parameters.
struct Params {
    float radius = 0.05f;
    float h = 0.3f;
    float inv_h = 1.0f / 0.3f;
    float rest_density = 1.0f;
    float inv_rest = 1.0f;
    float viscosity = 0.0f;
    float relaxation = 0.5f;
    float tensile = 0.0f;
    float inv_w_dq = 1.0f;
    int iterations = 4;
    int substeps = 2;
    float friction = 0.0f;
    float restitution = 0.0f;
    float max_delta = 0.05f;
    float density = 1.0f;  // world mass per unit area, for two-way coupling
    float cohesion = 0.0f;
    float vorticity = 0.0f;
};

// Poly6 density (self included) of a square lattice at `spacing` (in h).
[[nodiscard]] float lattice_density(float spacing) {
    const int reach = static_cast<int>(std::ceil(1.0f / spacing));
    float sum = 0.0f;
    for (int j = -reach; j <= reach; ++j) {
        for (int i = -reach; i <= reach; ++i) {
            const float x = static_cast<float>(i) * spacing;
            const float y = static_cast<float>(j) * spacing;
            sum += poly6(x * x + y * y);
        }
    }
    return sum;
}

[[nodiscard]] Params resolve(const Fluid2DParams& fluid) {
    Params p;
    p.radius = particle_radius(fluid);
    p.h = positive_or(fluid.smoothing_length, 6.0f * p.radius);
    p.inv_h = 1.0f / p.h;
    p.rest_density = positive_or(fluid.rest_density, lattice_density(2.0f * p.radius * p.inv_h));
    p.inv_rest = 1.0f / p.rest_density;
    p.viscosity = clamp01(fluid.viscosity);
    p.relaxation = positive_or(fluid.relaxation, 1.0e-3f);
    p.tensile = std::isfinite(fluid.tensile_strength) ? std::max(fluid.tensile_strength, 0.0f) : 0.0f;
    p.inv_w_dq = 1.0f / poly6(kTensileDq * kTensileDq);
    p.iterations = std::clamp(fluid.iterations, 1, 32);
    p.substeps = std::clamp(fluid.substeps, 1, 16);
    p.friction = clamp01(fluid.friction);
    p.restitution = clamp01(fluid.restitution);
    p.max_delta = kMaxDeltaRadii * p.radius;
    p.density = std::isfinite(fluid.density) ? std::max(fluid.density, 0.0f) : 0.0f;
    p.cohesion = std::isfinite(fluid.cohesion) ? std::max(fluid.cohesion, 0.0f) : 0.0f;
    p.vorticity = std::isfinite(fluid.vorticity) ? std::max(fluid.vorticity, 0.0f) : 0.0f;
    return p;
}

// Where `fluid` can be during a step of `dt` (world XY): its particles' box grown by a bound on how far any of them can
// move. Per substep a particle moves at most its speed times the substep plus the solver's reach (a radius of boundary
// push and `iterations` deltas and SDF pushes); what it moved becomes velocity, so the bound grows with each substep.
// False for an empty fluid.
[[nodiscard]] bool fluid_reach(const Fluid2D& fluid, glm::vec2 gravity, float dt, glm::vec2& lo, glm::vec2& hi) {
    if (fluid.count() == 0 || !(dt > 0.0f) || !std::isfinite(dt)) {
        return false;
    }
    const Params p = resolve(fluid.params);
    lo = glm::vec2{std::numeric_limits<float>::max()};
    hi = glm::vec2{-std::numeric_limits<float>::max()};
    float speed = 0.0f;
    for (std::size_t i = 0; i < fluid.count(); ++i) {
        lo = glm::min(lo, fluid.positions()[i]);
        hi = glm::max(hi, fluid.positions()[i]);
        speed = std::max(speed, glm::dot(fluid.velocities()[i], fluid.velocities()[i]));
    }
    speed = std::sqrt(speed);
    const float sub_dt = dt / static_cast<float>(p.substeps);
    const float g = finite(gravity) ? std::sqrt(glm::dot(gravity, gravity)) * std::abs(fluid.params.gravity_scale) : 0.0f;
    const float solver = p.radius + (p.max_delta + p.radius) * static_cast<float>(p.iterations);
    float travel = p.radius;
    for (int sub = 0; sub < p.substeps; ++sub) {
        speed += std::isfinite(g) ? g * sub_dt : 0.0f;
        const float move = speed * sub_dt + solver;
        travel += move;
        speed = move / sub_dt;
    }
    if (!std::isfinite(travel) || !finite(lo) || !finite(hi)) {
        lo = glm::vec2{-std::numeric_limits<float>::max()};
        hi = glm::vec2{std::numeric_limits<float>::max()};
        return true;
    }
    lo -= glm::vec2{travel};
    hi += glm::vec2{travel};
    return true;
}

// Selected shapes by grid cell, rebuilt per substep: a particle projects against the shapes listed in its cell only,
// so terrain made of many compound parts costs a particle what is near it. A shape is listed in every cell its box,
// grown by the particle radius, overlaps within the fluid's box. A cell lists its shapes in selection order.
struct ShapeGrid2D {
    float inv_cell = 1.0f;
    std::uint32_t mask = 0;
    std::vector<std::uint32_t> bucket_start;
    std::vector<glm::ivec2> entry_cell;
    std::vector<std::uint32_t> entry_shape;
    std::vector<std::pair<glm::ivec2, std::uint32_t>> pairs;

    [[nodiscard]] glm::ivec2 cell_of(glm::vec2 position) const {
        return {cell_coord(position.x * inv_cell), cell_coord(position.y * inv_cell)};
    }

    template <class F>
    void for_each(glm::vec2 position, F&& fn) const {
        if (entry_shape.empty()) {
            return;
        }
        const glm::ivec2 cell = cell_of(position);
        const std::uint32_t b = cell_hash(cell, mask);
        for (std::uint32_t k = bucket_start[b]; k < bucket_start[b + 1]; ++k) {
            if (entry_cell[k] == cell) {
                fn(entry_shape[k]);
            }
        }
    }
};

}

// Per-step buffers, reused across ticks and fluids. Not part of any fluid's state.
struct Fluid2DScratch {
    std::vector<glm::vec2> predicted;
    std::vector<glm::vec2> pre_velocity;
    std::vector<glm::vec2> delta;
    std::vector<glm::vec2> normal;  // last boundary normal this substep; zero when none
    std::vector<glm::vec2> velocity;
    std::vector<float> lambda;
    // Vorticity confinement: each particle's curl of velocity (1/s).
    std::vector<float> curl;
    // The group being stepped (LQ-62): fluids that touch are solved as one particle system. Particle i belongs to
    // member `owner[i]`, whose resolved parameters are `members[owner[i]]`; its radius, its mass relative to the
    // group's heaviest particle, and its 1 / rest density are copied per particle for the hot loops. A lone fluid is
    // a group of one: every ratio below is then exactly 1 and the result is the single-fluid solver's, bit for bit.
    std::vector<Params> members;
    std::vector<std::uint32_t> owner;
    std::vector<float> radius;
    std::vector<float> mass;
    std::vector<float> inv_rest;
    std::vector<glm::vec2> x;
    std::vector<glm::vec2> v;
    std::vector<float> density;
    // Per selected shape and SDF field: the members it bounds (bit k for member k).
    std::vector<std::uint32_t> shape_members;
    std::vector<std::uint32_t> sdf_members;
    std::vector<glm::ivec2> cell;
    std::vector<std::uint32_t> bucket_start;
    std::vector<std::uint32_t> sorted;
    std::vector<std::uint32_t> neighbor_count;
    std::vector<std::uint32_t> neighbors;
    // Per neighbour slot, this iteration: the spiky gradient (h units) and the poly6 weight.
    std::vector<glm::vec2> gradient;
    std::vector<float> weight;
    std::vector<const SdfBoundary*> sdf;
    std::vector<const ShapeBoundary*> shapes;
    ShapeGrid2D grid;
    // Two-way coupling, per particle and substep: up to kCouplingSlots bodies and how far each pushed the particle
    // (the sum of its pushes), with the moment arm of the last push. Only when some selected shape has a body.
    struct Push {
        int body = -1;
        glm::vec2 moved{0.0f};
        float moment = 0.0f;  // cross(contact - origin, moved), summed
        float arm = 0.0f;     // the longest |contact - origin|
    };
    std::vector<Push> pushes;
    bool coupled = false;
    const Boundaries* boundaries = nullptr;
    std::vector<std::uint32_t> grid_fill;  // scratch of the grid's counting sort
    // Where each particle's sweep starts: its position at the start of the substep for the first iteration, then where
    // the previous iteration left it (outside every boundary).
    std::vector<glm::vec2> from;
};

// The fluids `run_fluid_2d` steps as one particle system this tick, in view order, with their entities.
struct FluidGroup2D {
    std::vector<Fluid2D*> fluids;
    std::vector<ecs::Entity> entities;
};

struct Fluid2DSolver {
    static void step(const FluidGroup2D& group, Boundaries& boundaries, glm::vec2 gravity, float dt, JobSystem* jobs,
                     Fluid2DScratch& s, ecs::EventWriter<FluidContactEvent2D>& contacts);
    // Neighbour lists of `s.predicted` [0, n).
    static void build_neighbors(const Params& p, std::size_t n, JobSystem* jobs, Fluid2DScratch& s);
    static void select_boundaries(const FluidGroup2D& group, const Params& p, const Boundaries& boundaries,
                                  std::size_t n, Fluid2DScratch& s);
    static void sweep_range(std::size_t begin, std::size_t end, Fluid2DScratch& s);
    // Sources, before the step: emit what each enabled source owes for `dt`.
    static void emit(Fluid2D& fluid, float dt);
    // Drains, after the step: remove the particles that ended inside an enabled drain, keeping the others' order.
    static void drain(Fluid2D& fluid);
    // Sensors, after the drains: measure each fill and send an event when one turns full or stops being full.
    static void sense(Fluid2D& fluid, ecs::Entity entity, ecs::EventWriter<FluidSensorEvent2D>& events);
    // Two-way coupling: `shape` moved particle i by `moved`, leaving it at `position` with contact normal `normal`.
    static void record_push(std::size_t i, const ShapeBoundary& shape, glm::vec2 moved,
                            glm::vec2 position, glm::vec2 normal, Fluid2DScratch& s);
    static void project_range(std::size_t begin, std::size_t end, Fluid2DScratch& s);
};

namespace {

// The first time in [0, `best`) at which the segment `from + t * delta` enters the circle of `reach` about `center`.
// A start inside counts as a hit at 0 when the step goes further in. Sets `best` and `normal` on a hit.
[[nodiscard]] bool sweep_circle_shape(glm::vec2 from, glm::vec2 delta, glm::vec2 center, float reach, float& best,
                                      glm::vec2& normal) {
    const glm::vec2 m = from - center;
    const float c = glm::dot(m, m) - reach * reach;
    const float b = glm::dot(m, delta);
    if (b >= 0.0f) {
        return false;  // moving away from the center: no entry, and a start inside is left to the projection
    }
    if (c <= 0.0f) {
        const float length = std::sqrt(glm::dot(m, m));
        best = 0.0f;
        normal = length > 1.0e-6f ? m / length : glm::vec2{0.0f, 1.0f};
        return true;
    }
    const float a = glm::dot(delta, delta);
    const float disc = b * b - a * c;
    if (disc < 0.0f) {
        return false;
    }
    const float t = (-b - std::sqrt(disc)) / a;
    if (!(t >= 0.0f) || t >= best) {
        return false;
    }
    best = t;
    normal = glm::normalize(m + delta * t);
    return true;
}

// The same for the box of `half` extents about the origin of its own frame (the box grown by the radius, square
// corners). Slab test; the entry face gives the normal. A start inside counts at 0 when the step goes deeper through
// the face of least penetration.
[[nodiscard]] bool sweep_box_shape(glm::vec2 from, glm::vec2 delta, glm::vec2 half, float& best, glm::vec2& normal) {
    const float px = half.x - std::abs(from.x);
    const float py = half.y - std::abs(from.y);
    if (px > 0.0f && py > 0.0f) {
        glm::vec2 n{0.0f};
        if (px < py) {
            n.x = from.x < 0.0f ? -1.0f : 1.0f;
        } else {
            n.y = from.y < 0.0f ? -1.0f : 1.0f;
        }
        if (glm::dot(delta, n) >= 0.0f) {
            return false;
        }
        best = 0.0f;
        normal = n;
        return true;
    }
    float enter = 0.0f;
    float leave = best;
    glm::vec2 n{0.0f};
    for (int axis = 0; axis < 2; ++axis) {
        const float o = from[axis];
        const float d = delta[axis];
        const float h = half[axis];
        if (d == 0.0f) {
            if (o < -h || o > h) {
                return false;
            }
            continue;
        }
        float t0 = (-h - o) / d;
        float t1 = (h - o) / d;
        float side = -1.0f;
        if (t0 > t1) {
            std::swap(t0, t1);
            side = 1.0f;
        }
        if (t0 > enter) {
            enter = t0;
            n = glm::vec2{0.0f};
            n[axis] = side;
        }
        leave = std::min(leave, t1);
        if (enter > leave) {
            return false;
        }
    }
    if ((n.x == 0.0f && n.y == 0.0f) || enter >= best) {
        return false;
    }
    best = enter;
    normal = n;
    return true;
}

// The same for a convex polygon grown by `radius` (each edge moved out along its normal: sharp corners), clipping the
// segment against every edge plane (Cyrus-Beck). A start inside counts at 0 when the step goes deeper through the
// face of least penetration.
[[nodiscard]] bool sweep_polygon_shape(glm::vec2 from, glm::vec2 delta, const ShapeBoundary& shape, float radius,
                                       float& best, glm::vec2& normal) {
    float deepest = -std::numeric_limits<float>::max();
    int face = 0;
    for (int e = 0; e < shape.count; ++e) {
        const float side = glm::dot(from - shape.vertices[static_cast<std::size_t>(e)],
                                    shape.normals[static_cast<std::size_t>(e)]) - radius;
        if (side > deepest) {
            deepest = side;
            face = e;
        }
    }
    if (deepest < 0.0f) {
        const glm::vec2 n = shape.normals[static_cast<std::size_t>(face)];
        if (glm::dot(delta, n) >= 0.0f) {
            return false;
        }
        best = 0.0f;
        normal = n;
        return true;
    }
    float enter = 0.0f;
    float leave = best;
    glm::vec2 n{0.0f};
    for (int e = 0; e < shape.count; ++e) {
        const glm::vec2 en = shape.normals[static_cast<std::size_t>(e)];
        const float num = radius - glm::dot(from - shape.vertices[static_cast<std::size_t>(e)], en);
        const float den = glm::dot(delta, en);
        if (den == 0.0f) {
            if (num < 0.0f) {
                return false;
            }
            continue;
        }
        const float t = num / den;
        if (den < 0.0f) {
            if (t > enter) {
                enter = t;
                n = en;
            }
        } else {
            leave = std::min(leave, t);
        }
        if (enter > leave) {
            return false;
        }
    }
    if ((n.x == 0.0f && n.y == 0.0f) || enter >= best) {
        return false;
    }
    best = enter;
    normal = n;
    return true;
}

// Pushes `pos` out of the convex polygon `shape` to `radius` from it: along the face of least penetration from inside,
// from the nearest boundary point outside. False when it is at least `radius` away.
[[nodiscard]] bool project_polygon(const ShapeBoundary& shape, float radius, glm::vec2& pos, glm::vec2& normal) {
    float deepest = -std::numeric_limits<float>::max();
    int face = 0;
    for (int e = 0; e < shape.count; ++e) {
        const float side =
                glm::dot(pos - shape.vertices[static_cast<std::size_t>(e)], shape.normals[static_cast<std::size_t>(e)]);
        if (side > deepest) {
            deepest = side;
            face = e;
        }
    }
    if (deepest >= radius) {
        return false;  // a face plane at least `radius` away: the convex polygon is at least that far
    }
    if (deepest <= 0.0f) {
        const glm::vec2 n = shape.normals[static_cast<std::size_t>(face)];
        pos += n * (radius - deepest);
        normal = n;
        return true;
    }
    glm::vec2 closest{0.0f};
    float closest_d2 = std::numeric_limits<float>::max();
    for (int e = 0; e < shape.count; ++e) {
        const glm::vec2 a = shape.vertices[static_cast<std::size_t>(e)];
        const glm::vec2 b = shape.vertices[static_cast<std::size_t>((e + 1) % shape.count)];
        const glm::vec2 ab = b - a;
        const float t = std::clamp(glm::dot(pos - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        const glm::vec2 q = a + ab * t;
        const float d2 = glm::dot(pos - q, pos - q);
        if (d2 < closest_d2) {
            closest_d2 = d2;
            closest = q;
        }
    }
    if (closest_d2 >= radius * radius) {
        return false;
    }
    const float d = std::sqrt(closest_d2);
    const glm::vec2 n = d > 1.0e-6f ? (pos - closest) / d : shape.normals[static_cast<std::size_t>(face)];
    pos = closest + n * radius;
    normal = n;
    return true;
}

}

void Fluid2DSolver::build_neighbors(const Params& p, std::size_t n, JobSystem* jobs, Fluid2DScratch& s) {
    std::uint32_t table = 64;
    while (static_cast<std::size_t>(table) < 2 * n) {
        table *= 2;
    }
    const std::uint32_t mask = table - 1;
    s.cell.resize(n);
    s.bucket_start.assign(static_cast<std::size_t>(table) + 1, 0);
    s.sorted.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 q = s.predicted[i] * p.inv_h;
        s.cell[i] = glm::ivec2{cell_coord(q.x), cell_coord(q.y)};
        ++s.bucket_start[cell_hash(s.cell[i], mask) + 1];
    }
    for (std::uint32_t b = 0; b < table; ++b) {
        s.bucket_start[b + 1] += s.bucket_start[b];
    }
    // Counting sort, stable: a bucket lists its particles in index order. The neighbour counts are free until below.
    std::vector<std::uint32_t>& fill = s.neighbor_count;
    fill.assign(table, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t b = cell_hash(s.cell[i], mask);
        s.sorted[s.bucket_start[b] + fill[b]++] = static_cast<std::uint32_t>(i);
    }

    s.neighbor_count.assign(n, 0);
    s.neighbors.resize(n * kMaxNeighbors);
    const float h2 = p.h * p.h;
    for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            const glm::vec2 pi = s.predicted[i];
            const glm::ivec2 ci = s.cell[i];
            std::uint32_t* out = &s.neighbors[i * kMaxNeighbors];
            std::uint32_t count = 0;
            for (int dy = -1; dy <= 1 && count < kMaxNeighbors; ++dy) {
                for (int dx = -1; dx <= 1 && count < kMaxNeighbors; ++dx) {
                    const glm::ivec2 c{ci.x + dx, ci.y + dy};
                    const std::uint32_t b = cell_hash(c, mask);
                    for (std::uint32_t k = s.bucket_start[b]; k < s.bucket_start[b + 1]; ++k) {
                        const std::uint32_t j = s.sorted[k];
                        if (j == i || s.cell[j] != c) {
                            continue;
                        }
                        const glm::vec2 d = pi - s.predicted[j];
                        if (glm::dot(d, d) < h2) {
                            out[count++] = j;
                            if (count == kMaxNeighbors) {
                                break;
                            }
                        }
                    }
                }
            }
            s.neighbor_count[i] = count;
        }
    });
}

void Fluid2DSolver::select_boundaries(const FluidGroup2D& group, const Params& p, const Boundaries& boundaries,
                                      std::size_t n, Fluid2DScratch& s) {
    s.sdf.clear();
    s.shapes.clear();
    s.sdf_members.clear();
    s.shape_members.clear();
    // The members a collider bounds, under the layer rule of each.
    const auto members_of = [&](uint32_t layer, uint32_t mask) {
        std::uint32_t bits = 0;
        for (std::size_t k = 0; k < group.fluids.size(); ++k) {
            if (layers_match(group.fluids[k]->params, layer, mask)) {
                bits |= 1u << k;
            }
        }
        return bits;
    };
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{-std::numeric_limits<float>::max()};
    // The substep starts too: a swept step meets every boundary between its start and its end.
    for (std::size_t i = 0; i < n; ++i) {
        lo = glm::min(lo, glm::min(s.predicted[i], s.from[i]));
        hi = glm::max(hi, glm::max(s.predicted[i], s.from[i]));
    }
    // Per iteration the solver moves a particle at most `max_delta`, and an SDF projection at most a radius.
    const float grow = p.radius + (p.max_delta + p.radius) * static_cast<float>(p.iterations);
    lo -= glm::vec2{grow};
    hi += glm::vec2{grow};
    for (const SdfBoundary& sdf : boundaries.sdf) {
        if (const std::uint32_t bits = members_of(sdf.layer, sdf.mask); bits != 0) {
            s.sdf.push_back(&sdf);
            s.sdf_members.push_back(bits);
        }
    }
    for (const ShapeBoundary& shape : boundaries.shapes) {
        if (shape.hi.x < lo.x || shape.lo.x > hi.x || shape.hi.y < lo.y || shape.lo.y > hi.y) {
            continue;
        }
        if (const std::uint32_t bits = members_of(shape.layer, shape.mask); bits != 0) {
            s.shapes.push_back(&shape);
            s.shape_members.push_back(bits);
        }
    }

    // The grid: cells of two kernel supports, each shape in the cells its grown box covers inside the fluid's box.
    ShapeGrid2D& grid = s.grid;
    grid.inv_cell = 0.5f * p.inv_h;
    grid.pairs.clear();
    const glm::ivec2 fluid_lo = grid.cell_of(lo);
    const glm::ivec2 fluid_hi = grid.cell_of(hi);
    for (std::size_t k = 0; k < s.shapes.size(); ++k) {
        const ShapeBoundary& shape = *s.shapes[k];
        const glm::ivec2 c0 = glm::max(grid.cell_of(shape.lo - glm::vec2{p.radius}), fluid_lo);
        const glm::ivec2 c1 = glm::min(grid.cell_of(shape.hi + glm::vec2{p.radius}), fluid_hi);
        for (int y = c0.y; y <= c1.y; ++y) {
            for (int x = c0.x; x <= c1.x; ++x) {
                grid.pairs.emplace_back(glm::ivec2{x, y}, static_cast<std::uint32_t>(k));
            }
        }
    }
    std::uint32_t table = 64;
    while (static_cast<std::size_t>(table) < 2 * grid.pairs.size()) {
        table *= 2;
    }
    grid.mask = table - 1;
    grid.bucket_start.assign(static_cast<std::size_t>(table) + 1, 0);
    for (const auto& [cell, shape] : grid.pairs) {
        ++grid.bucket_start[cell_hash(cell, grid.mask) + 1];
    }
    for (std::uint32_t b = 0; b < table; ++b) {
        grid.bucket_start[b + 1] += grid.bucket_start[b];
    }
    grid.entry_cell.resize(grid.pairs.size());
    grid.entry_shape.resize(grid.pairs.size());
    std::vector<std::uint32_t>& fill = s.grid_fill;
    fill.assign(table, 0);
    for (const auto& [cell, shape] : grid.pairs) {
        const std::uint32_t b = cell_hash(cell, grid.mask);
        const std::uint32_t at = grid.bucket_start[b] + fill[b]++;
        grid.entry_cell[at] = cell;
        grid.entry_shape[at] = shape;
    }
}

void Fluid2DSolver::record_push(std::size_t i, const ShapeBoundary& shape, glm::vec2 moved,
                                glm::vec2 position, glm::vec2 normal, Fluid2DScratch& s) {
    if (shape.body < 0 || !s.coupled) {
        return;
    }
    const glm::vec2 contact = position - normal * s.radius[i];
    const glm::vec2 arm = contact - s.boundaries->bodies[static_cast<std::size_t>(shape.body)].origin;
    const float moment = arm.x * moved.y - arm.y * moved.x;
    Fluid2DScratch::Push* slots = &s.pushes[i * kCouplingSlots];
    std::size_t k = 0;
    while (k + 1 < kCouplingSlots && slots[k].body >= 0 && slots[k].body != shape.body) {
        ++k;
    }
    if (slots[k].body < 0) {
        slots[k].body = shape.body;
    }
    slots[k].moved += moved;
    slots[k].moment += moment;
    slots[k].arm = std::max(slots[k].arm, std::sqrt(glm::dot(arm, arm)));
}

// Sweeps the step of each particle in [begin, end) that moved more than `kSweepRadii` radii in this iteration (from
// `from`) against every selected boundary, and stops it at the first contact: a circle (its radius plus the particle's), a box
// grown by the radius (square corners), an SDF field (`SdfField2D::sweep_circle`). A particle that starts in contact
// and moves further in stops where it started. The stop is `kSweepSkinRadii` radii off the surface along its normal,
// which becomes the particle's contact normal; `project_range` resolves the rest. A particle's result depends only on
// itself and the read-only boundaries.
void Fluid2DSolver::sweep_range(std::size_t begin, std::size_t end, Fluid2DScratch& s) {
    for (std::size_t i = begin; i < end; ++i) {
        const float radius_i = s.radius[i];
        const std::uint32_t member = 1u << s.owner[i];
        const float threshold = kSweepRadii * radius_i;
        const glm::vec2 from = s.from[i];
        const glm::vec2 delta = s.predicted[i] - from;
        if (glm::dot(delta, delta) <= threshold * threshold || !finite(delta)) {
            continue;
        }
        const glm::vec2 lo = glm::min(from, from + delta) - glm::vec2{radius_i};
        const glm::vec2 hi = glm::max(from, from + delta) + glm::vec2{radius_i};
        float best = 1.0f;
        glm::vec2 normal{0.0f};
        const ShapeBoundary* stopper = nullptr;
        for (std::size_t index = 0; index < s.shapes.size(); ++index) {
            const ShapeBoundary* shape = s.shapes[index];
            if ((s.shape_members[index] & member) == 0 || hi.x < shape->lo.x || lo.x > shape->hi.x ||
                hi.y < shape->lo.y || lo.y > shape->hi.y) {
                continue;
            }
            const float before = best;
            if (shape->kind == ShapeBoundary::Kind::Circle) {
                (void)sweep_circle_shape(from, delta, shape->frame.position, shape->radius + radius_i, best, normal);
            } else if (shape->kind == ShapeBoundary::Kind::Polygon) {
                (void)sweep_polygon_shape(from, delta, *shape, radius_i, best, normal);
            } else {
                glm::vec2 local_normal{0.0f};
                if (sweep_box_shape(shape->frame.to_local(from), shape->frame.rotate_to_local(delta),
                                    shape->half + glm::vec2{radius_i}, best, local_normal)) {
                    normal = shape->frame.rotate_to_world(local_normal);
                }
            }
            if (best < before) {
                stopper = shape;
            }
        }
        for (std::size_t index = 0; index < s.sdf.size(); ++index) {
            if ((s.sdf_members[index] & member) == 0) {
                continue;
            }
            const SdfBoundary* sdf = s.sdf[index];
            const SdfField2D& field = *sdf->field;
            const float radius = std::min(radius_i, field.band() * 0.999f);
            const glm::vec2 local_from = sdf->frame.to_local(from);
            const glm::vec2 local_delta = sdf->frame.rotate_to_local(delta);
            if (field.outside() != SdfOutside::Solid) {
                const std::optional<SdfBounds2D> bounds = field.allocated_bounds();
                if (!bounds) {
                    continue;
                }
                const glm::vec2 a = glm::min(local_from, local_from + local_delta) - glm::vec2{radius};
                const glm::vec2 b = glm::max(local_from, local_from + local_delta) + glm::vec2{radius};
                if (b.x < bounds->min.x || a.x > bounds->max.x || b.y < bounds->min.y || a.y > bounds->max.y) {
                    continue;
                }
            }
            const std::optional<SdfSweep2D> hit = field.sweep_circle(local_from, radius, local_delta);
            if (!hit || (hit->normal.x == 0.0f && hit->normal.y == 0.0f) || hit->fraction >= best) {
                continue;
            }
            if (hit->fraction == 0.0f && glm::dot(local_delta, hit->normal) >= 0.0f) {
                continue;  // starts in contact and moves away: nothing to stop
            }
            best = hit->fraction;
            normal = sdf->frame.rotate_to_world(hit->normal);
            stopper = nullptr;
        }
        if (best < 1.0f) {
            const glm::vec2 stop = from + delta * best + normal * (kSweepSkinRadii * radius_i);
            if (stopper != nullptr) {
                record_push(i, *stopper, stop - s.predicted[i], stop, normal, s);
            }
            s.predicted[i] = stop;
            s.normal[i] = normal;
        }
    }
}

// Pushes predicted positions [begin, end) out of every selected boundary: boxes and circles, then SDF colliders in
// gather order, so a particle squeezed between a moving shape and the terrain ends outside the terrain (and is pushed
// out of the shape on a later iteration). A particle's result depends only on itself and the read-only boundaries.
void Fluid2DSolver::project_range(std::size_t begin, std::size_t end, Fluid2DScratch& s) {
    for (std::size_t i = begin; i < end && !s.shapes.empty(); ++i) {
        glm::vec2 pos = s.predicted[i];
        const float radius_i = s.radius[i];
        const std::uint32_t member = 1u << s.owner[i];
        s.grid.for_each(pos, [&](std::uint32_t index) {
            const ShapeBoundary* shape = s.shapes[index];
            if ((s.shape_members[index] & member) == 0 || pos.x < shape->lo.x - radius_i ||
                pos.x > shape->hi.x + radius_i || pos.y < shape->lo.y - radius_i || pos.y > shape->hi.y + radius_i) {
                return;
            }
            const glm::vec2 before = pos;
            if (shape->kind == ShapeBoundary::Kind::Polygon) {
                if (project_polygon(*shape, radius_i, pos, s.normal[i])) {
                    record_push(i, *shape, pos - before, pos, s.normal[i], s);
                }
            } else if (shape->kind == ShapeBoundary::Kind::Circle) {
                const glm::vec2 d = pos - shape->frame.position;
                const float reach = shape->radius + radius_i;
                const float dist2 = glm::dot(d, d);
                if (dist2 >= reach * reach) {
                    return;
                }
                const float dist = std::sqrt(dist2);
                const glm::vec2 n = dist > 1.0e-6f ? d / dist : glm::vec2{0.0f, 1.0f};
                pos = shape->frame.position + n * reach;
                s.normal[i] = n;
                record_push(i, *shape, pos - before, pos, n, s);
            } else {
                const glm::vec2 l = shape->frame.to_local(pos);
                const glm::vec2 half = shape->half + glm::vec2{radius_i};
                const float px = half.x - std::abs(l.x);
                const float py = half.y - std::abs(l.y);
                if (px <= 0.0f || py <= 0.0f) {
                    return;
                }
                glm::vec2 n{0.0f};
                glm::vec2 out = l;
                if (px < py) {
                    n.x = l.x < 0.0f ? -1.0f : 1.0f;
                    out.x = n.x * half.x;
                } else {
                    n.y = l.y < 0.0f ? -1.0f : 1.0f;
                    out.y = n.y * half.y;
                }
                pos = shape->frame.to_world(out);
                s.normal[i] = shape->frame.rotate_to_world(n);
                record_push(i, *shape, pos - before, pos, s.normal[i], s);
            }
        });
        s.predicted[i] = pos;
    }
    std::array<glm::vec2, kSdfBatch> local{};
    std::array<float, kSdfBatch> distance{};
    std::array<std::uint32_t, kSdfBatch> index{};
    for (std::size_t field_index = 0; field_index < s.sdf.size(); ++field_index) {
        const SdfBoundary* sdf = s.sdf[field_index];
        const std::uint32_t members = s.sdf_members[field_index];
        const SdfField2D& field = *sdf->field;
        const float band_radius = field.band() * 0.999f;
        const std::optional<SdfBounds2D> bounds = field.allocated_bounds();
        const bool solid_outside = field.outside() == SdfOutside::Solid;
        if (!bounds && !solid_outside) {
            continue;
        }
        for (std::size_t batch = begin; batch < end; batch += kSdfBatch) {
            const std::size_t batch_end = std::min(end, batch + kSdfBatch);
            std::size_t count = 0;
            for (std::size_t i = batch; i < batch_end; ++i) {
                if ((members & (1u << s.owner[i])) == 0) {
                    continue;
                }
                const float radius = std::min(s.radius[i], band_radius);
                const glm::vec2 l = sdf->frame.to_local(s.predicted[i]);
                if (!solid_outside && (l.x < bounds->min.x - radius || l.x > bounds->max.x + radius ||
                                       l.y < bounds->min.y - radius || l.y > bounds->max.y + radius)) {
                    continue;
                }
                local[count] = l;
                index[count] = static_cast<std::uint32_t>(i);
                ++count;
            }
            if (count == 0) {
                continue;
            }
            field.sample_many(std::span<const glm::vec2>{local.data(), count},
                              std::span<float>{distance.data(), count});
            for (std::size_t k = 0; k < count; ++k) {
                const float d = distance[k];
                const std::uint32_t i = index[k];
                const float radius = std::min(s.radius[i], band_radius);
                if (d >= radius) {
                    continue;
                }
                const glm::vec2 g = field.gradient(local[k]);
                if (g.x == 0.0f && g.y == 0.0f) {
                    continue;
                }
                s.predicted[i] = sdf->frame.to_world(local[k] + g * (radius - d));
                s.normal[i] = sdf->frame.rotate_to_world(g);
            }
        }
    }
}

void Fluid2DSolver::step(const FluidGroup2D& group, Boundaries& boundaries, glm::vec2 gravity, float dt,
                         JobSystem* jobs, Fluid2DScratch& s, ecs::EventWriter<FluidContactEvent2D>& contacts) {
    std::size_t n = 0;
    for (Fluid2D* fluid : group.fluids) {
        fluid->densities_.resize(fluid->positions_.size(), 0.0f);
        n += fluid->positions_.size();
    }
    if (n == 0 || !(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }
    // The group's parameters: each member's own, and for the whole system the largest kernel support (pair terms
    // must be symmetric) and the most substeps and iterations. `p` is the first member's with those shared fields, and
    // the largest radius and delta clamp for the boundary culls. For a group of one it is that fluid's own.
    s.members.clear();
    for (Fluid2D* fluid : group.fluids) {
        s.members.push_back(resolve(fluid->params));
    }
    Params p = s.members.front();
    for (const Params& member : s.members) {
        p.h = std::max(p.h, member.h);
        p.substeps = std::max(p.substeps, member.substeps);
        p.iterations = std::max(p.iterations, member.iterations);
        p.radius = std::max(p.radius, member.radius);
        p.max_delta = std::max(p.max_delta, member.max_delta);
    }
    p.inv_h = 1.0f / p.h;
    if (s.members.size() > 1) {
        // Rest densities of each member's own lattice at the shared support (the lone-fluid value otherwise).
        for (std::size_t k = 0; k < s.members.size(); ++k) {
            Params& member = s.members[k];
            member.rest_density = positive_or(group.fluids[k]->params.rest_density,
                                              lattice_density(2.0f * member.radius * p.inv_h));
            member.inv_rest = 1.0f / member.rest_density;
        }
    }
    // Relative particle masses, the heaviest 1: each particle takes its inverse mass's share of every density
    // correction, so at an interface the lighter fluid yields and the heavier sinks. A fluid with density 0 counts as
    // the heaviest's thousandth.
    float heaviest = 0.0f;
    std::vector<float> member_mass(s.members.size());
    for (std::size_t k = 0; k < s.members.size(); ++k) {
        const float spacing = 2.0f * s.members[k].radius;
        member_mass[k] = s.members[k].density * spacing * spacing;
        heaviest = std::max(heaviest, member_mass[k]);
    }
    std::vector<float> relative(s.members.size(), 1.0f);
    if (s.members.size() > 1 && heaviest > 0.0f) {
        for (std::size_t k = 0; k < s.members.size(); ++k) {
            relative[k] = std::max(member_mass[k] / heaviest, 1.0e-3f);
        }
    }
    const bool mixed = s.members.size() > 1;

    // Gather the members' particles in member order.
    s.x.resize(n);
    s.v.resize(n);
    s.density.resize(n);
    s.owner.resize(n);
    s.radius.resize(n);
    s.mass.resize(n);
    s.inv_rest.resize(n);
    {
        std::size_t at = 0;
        for (std::size_t k = 0; k < group.fluids.size(); ++k) {
            Fluid2D& fluid = *group.fluids[k];
            fluid.query_valid_ = false;
            for (std::size_t i = 0; i < fluid.positions_.size(); ++i, ++at) {
                s.x[at] = fluid.positions_[i];
                s.v[at] = fluid.velocities_[i];
                s.density[at] = fluid.densities_[i];
                s.owner[at] = static_cast<std::uint32_t>(k);
                s.radius[at] = s.members[k].radius;
                s.mass[at] = relative[k];
                s.inv_rest[at] = s.members[k].inv_rest;
            }
        }
    }
    const float sub_dt = dt / static_cast<float>(p.substeps);
    const float inv_dt = 1.0f / sub_dt;
    std::vector<glm::vec2> member_gravity(s.members.size());
    for (std::size_t k = 0; k < s.members.size(); ++k) {
        const float scale = std::isfinite(group.fluids[k]->params.gravity_scale) ? group.fluids[k]->params.gravity_scale
                                                                                : 0.0f;
        member_gravity[k] = finite(gravity) ? gravity * scale : glm::vec2{0.0f};
    }
    std::vector<glm::vec2>& x = s.x;
    std::vector<glm::vec2>& v = s.v;
    std::vector<float>& density = s.density;
    s.predicted.resize(n);
    s.pre_velocity.resize(n);
    s.delta.resize(n);
    s.normal.resize(n);
    s.velocity.resize(n);
    s.lambda.resize(n);
    s.from.resize(n);
    s.boundaries = &boundaries;
    // A particle's mass in world units: its fluid's density times the area of one lattice cell (the rest spacing
    // squared). The reaction of a push `moved` over a substep is an impulse of mass * moved / sub_dt on the body.
    const bool any_mass = std::any_of(member_mass.begin(), member_mass.end(), [](float m) { return m > 0.0f; });

    for (int sub = 0; sub < p.substeps; ++sub) {
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                v[i] += member_gravity[s.owner[i]] * sub_dt;
                s.pre_velocity[i] = v[i];
                s.predicted[i] = x[i] + v[i] * sub_dt;
                s.from[i] = x[i];
                s.normal[i] = glm::vec2{0.0f};
            }
        });
        build_neighbors(p, n, jobs, s);
        s.gradient.resize(n * kMaxNeighbors);
        s.weight.resize(n * kMaxNeighbors);
        select_boundaries(group, p, boundaries, n, s);
        s.coupled = any_mass &&
                    std::any_of(s.shapes.begin(), s.shapes.end(), [](const ShapeBoundary* shape) { return shape->body >= 0; });
        if (s.coupled) {
            s.pushes.assign(n * kCouplingSlots, Fluid2DScratch::Push{});
        }

        for (int iteration = 0; iteration < p.iterations; ++iteration) {
            // Lambda: the density constraint C_i = rho_i / rho_0 - 1 >= 0 (only compression pushes). Keeps each
            // neighbour's kernel gradient and weight for the delta pass, which reads the same positions.
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const Params& own = s.members[s.owner[i]];
                    const float inv_rest = s.inv_rest[i];
                    const float inv_mass = 1.0f / s.mass[i];
                    const glm::vec2 pi = s.predicted[i];
                    const std::size_t base = i * kMaxNeighbors;
                    const std::uint32_t* nb = &s.neighbors[base];
                    glm::vec2* grads = &s.gradient[base];
                    float* weights = &s.weight[base];
                    float rho = kPoly6;
                    glm::vec2 grad_i{0.0f};
                    float sum_sq = 0.0f;
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
                        const float r2 = glm::dot(d, d);
                        const float w = poly6(r2);
                        const glm::vec2 grad = spiky_grad(d, std::sqrt(r2));
                        weights[k] = w;
                        grads[k] = grad;
                        rho += w;
                        grad_i += grad;
                        // Each particle's share of the correction is its inverse mass (heaviest 1): a neighbour's
                        // gradient counts by its weight. 1 in a lone fluid.
                        sum_sq += mixed ? glm::dot(grad, grad) / s.mass[j] : glm::dot(grad, grad);
                    }
                    // Both sums in units of rest density: grad C = grad W / rho_0.
                    const float own_share = mixed ? glm::dot(grad_i, grad_i) * inv_mass : glm::dot(grad_i, grad_i);
                    sum_sq = (sum_sq + own_share) * (inv_rest * inv_rest);
                    const float ratio = rho * inv_rest;
                    density[i] = ratio;
                    const float c = ratio - 1.0f;
                    s.lambda[i] = c > 0.0f ? -c / (sum_sq + own.relaxation) : 0.0f;
                }
            });
            // Delta: gathered from the neighbours' lambdas, with the s_corr tensile term.
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const Params& own = s.members[s.owner[i]];
                    const float li = s.lambda[i];
                    const float inv_rest = s.inv_rest[i];
                    const float inv_mass = 1.0f / s.mass[i];
                    const std::size_t base = i * kMaxNeighbors;
                    const std::uint32_t* nb = &s.neighbors[base];
                    const glm::vec2* grads = &s.gradient[base];
                    const float* weights = &s.weight[base];
                    glm::vec2 sum{0.0f};
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const float w = weights[k] * own.inv_w_dq;
                        const float w2 = w * w;
                        const float scorr = -own.tensile * w2 * w2;
                        // The neighbour's constraint in units of its own rest density. 1 in a lone fluid.
                        const float rr = mixed ? s.inv_rest[j] / inv_rest : 1.0f;
                        sum += (li + s.lambda[j] * rr + scorr) * grads[k];
                    }
                    // i takes its inverse mass's share of every correction: a lighter particle yields to a heavier
                    // one, so the heavier sinks through it (buoyancy). 1 in a lone fluid.
                    glm::vec2 delta = mixed ? sum * (inv_rest * p.h * inv_mass) : sum * (inv_rest * p.h);
                    const float len2 = glm::dot(delta, delta);
                    if (len2 > own.max_delta * own.max_delta) {
                        delta *= own.max_delta / std::sqrt(len2);
                    }
                    s.delta[i] = delta;
                }
            });
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    s.predicted[i] += s.delta[i];
                }
                sweep_range(begin, end, s);
                project_range(begin, end, s);
                for (std::size_t i = begin; i < end; ++i) {
                    s.from[i] = s.predicted[i];
                }
            });
        }

        // Velocity from the solved positions, boundary response, then XSPH viscosity (gathered from the
        // neighbours' velocities into a second buffer).
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const Params& own = s.members[s.owner[i]];
                glm::vec2 vel = (s.predicted[i] - x[i]) * inv_dt;
                const glm::vec2 nrm = s.normal[i];
                if (nrm.x != 0.0f || nrm.y != 0.0f) {
                    float vn = glm::dot(vel, nrm);
                    const float approach = glm::dot(s.pre_velocity[i], nrm);
                    if (approach < 0.0f) {
                        vn = std::max(vn, -own.restitution * approach);
                    }
                    const glm::vec2 vt = vel - glm::dot(vel, nrm) * nrm;
                    vel = vn * nrm + vt * (1.0f - own.friction);
                }
                s.velocity[i] = vel;
            }
        });
        // Vorticity confinement needs every neighbour's curl first: w_i = sum_j grad_i W_ij x (v_j - v_i) / rho_0 / h,
        // the SPH curl (1/s; 2 omega for a rigid rotation at omega). grad_i W_ij points toward j.
        const bool swirling = std::any_of(s.members.begin(), s.members.end(),
                                          [](const Params& member) { return member.vorticity > 0.0f; });
        if (swirling) {
            s.curl.resize(n);
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const glm::vec2 pi = s.predicted[i];
                    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
                    float curl = 0.0f;
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
                        const glm::vec2 grad = spiky_grad(d, std::sqrt(glm::dot(d, d)));
                        const glm::vec2 dv = s.velocity[j] - s.velocity[i];
                        curl += grad.x * dv.y - grad.y * dv.x;
                    }
                    s.curl[i] = curl * s.inv_rest[i] * p.inv_h;
                }
            });
        }
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const Params& own = s.members[s.owner[i]];
                glm::vec2 vel = s.velocity[i];
                if (own.viscosity > 0.0f || own.cohesion > 0.0f || own.vorticity > 0.0f) {
                    const glm::vec2 pi = s.predicted[i];
                    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
                    glm::vec2 blend{0.0f};
                    glm::vec2 pull{0.0f};
                    glm::vec2 eta{0.0f};
                    const float rho_i = density[i] > 0.0f ? density[i] : 1.0f;
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
                        const float r2 = glm::dot(d, d);
                        blend += (s.velocity[j] - s.velocity[i]) * poly6(r2);
                        if (own.cohesion > 0.0f && s.owner[j] == s.owner[i]) {
                            // Akinci's cohesion K_ij C(r) r_hat, K_ij = 2 rho_0 / (rho_i + rho_j): pulls i toward its
                            // neighbours where it has fewer of them, at the free surface. Akinci's curvature term
                            // (n_i - n_j) is left out: explicit in this solver it throws out the corners of a blob.
                            const float r = std::sqrt(r2);
                            const float rho_j = density[j] > 0.0f ? density[j] : 1.0f;
                            const float k_ij = 2.0f / (rho_i + rho_j);
                            const glm::vec2 r_hat = r > 1.0e-6f ? d / r : glm::vec2{0.0f};
                            pull += (k_ij * cohesion_spline(r)) * r_hat;
                        }
                        if (own.vorticity > 0.0f) {
                            eta += (std::abs(s.curl[j]) - std::abs(s.curl[i])) * spiky_grad(d, std::sqrt(r2));
                        }
                    }
                    vel += blend * (own.viscosity * s.inv_rest[i]);
                    // Accelerations in world units: cohesion in h per second squared per unit of `cohesion`.
                    vel -= pull * (own.cohesion * p.h * sub_dt);
                    if (own.vorticity > 0.0f) {
                        // Confinement: f = eps (N x w), N the unit gradient of |w| (the SPH gradient
                        // sum_j (|w_j| - |w_i|) grad_i W_ij), toward the vortex center.
                        const float length = std::sqrt(glm::dot(eta, eta));
                        if (length > 1.0e-6f) {
                            const glm::vec2 normal = eta / length;
                            const float w = s.curl[i];
                            vel += glm::vec2{normal.y * w, -normal.x * w} * (own.vorticity * sub_dt);
                        }
                    }
                }
                if (mixed) {
                    // Buoyancy (LQ-62): a particle among heavier ones is lifted, among lighter ones it sinks, by
                    // Archimedes' a = -g (m_around - m_i) / m_i, with m_around the kernel-weighted mean mass of the
                    // particle and its neighbours. Zero inside one fluid. Clamped to two g either way.
                    const glm::vec2 pi = s.predicted[i];
                    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
                    float weight = kPoly6;
                    float mass_sum = kPoly6 * s.mass[i];
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
                        const float w = poly6(glm::dot(d, d));
                        weight += w;
                        mass_sum += w * s.mass[j];
                    }
                    const float around = mass_sum / weight;
                    const float lift = std::clamp((around - s.mass[i]) / s.mass[i], -2.0f, 2.0f);
                    vel -= member_gravity[s.owner[i]] * (lift * sub_dt);
                }
                v[i] = vel;
                x[i] = s.predicted[i];
            }
        });

        // Two-way coupling: the bodies take the reaction of every push, summed serially in particle order, so the
        // result does not depend on the worker count.
        if (s.coupled) {
            for (std::size_t i = 0; i < n; ++i) {
                const float to_impulse = -member_mass[s.owner[i]] * inv_dt;
                for (std::size_t k = 0; k < kCouplingSlots; ++k) {
                    const Fluid2DScratch::Push& push = s.pushes[i * kCouplingSlots + k];
                    if (push.body < 0) {
                        break;
                    }
                    CouplingBody2D& body = boundaries.bodies[static_cast<std::size_t>(push.body)];
                    body.impulse += push.moved * to_impulse;
                    body.angular_impulse += push.moment * to_impulse;
                    body.particle_speed = std::max(body.particle_speed, std::sqrt(glm::dot(v[i], v[i])));
                    body.arm = std::max(body.arm, push.arm);
                }
            }
        }
    }

    // Contacts between members (LQ-62): every neighbour pair of the last substep whose particles belong to two
    // fluids, counted once (lower index first), summed serially in particle order per pair of fluids.
    if (mixed) {
        struct Contact {
            std::size_t count = 0;
            glm::vec2 sum{0.0f};
            glm::vec2 lo{std::numeric_limits<float>::max()};
            glm::vec2 hi{-std::numeric_limits<float>::max()};
        };
        const std::size_t m = s.members.size();
        std::vector<Contact> pairs(m * m);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
            for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                const std::uint32_t j = nb[k];
                if (j <= i || s.owner[j] == s.owner[i]) {
                    continue;
                }
                const std::size_t a = std::min(s.owner[i], s.owner[j]);
                const std::size_t b = std::max(s.owner[i], s.owner[j]);
                Contact& contact = pairs[a * m + b];
                const glm::vec2 mid = (x[i] + x[j]) * 0.5f;
                ++contact.count;
                contact.sum += mid;
                contact.lo = glm::min(contact.lo, mid);
                contact.hi = glm::max(contact.hi, mid);
            }
        }
        for (std::size_t a = 0; a < m; ++a) {
            for (std::size_t b = a + 1; b < m; ++b) {
                const Contact& contact = pairs[a * m + b];
                if (contact.count == 0) {
                    continue;
                }
                contacts.send(FluidContactEvent2D{
                        .a = group.entities[a],
                        .b = group.entities[b],
                        .count = contact.count,
                        .center = contact.sum / static_cast<float>(contact.count),
                        .min = contact.lo,
                        .max = contact.hi,
                });
            }
        }
    }

    // Scatter the particles back, in member order.
    std::size_t at = 0;
    for (Fluid2D* fluid : group.fluids) {
        for (std::size_t i = 0; i < fluid->positions_.size(); ++i, ++at) {
            fluid->positions_[i] = x[at];
            fluid->velocities_[i] = v[at];
            fluid->densities_[i] = density[at];
        }
    }
}

namespace {

// The R2 low-discrepancy sequence (Roberts 2018), point `n` in [0, 1)^2: fractional parts of n / g and n / g^2 for the
// plastic number g, in double so a long stream stays spread. Only + * floor: the same on every target.
[[nodiscard]] glm::vec2 r2_point(std::uint64_t n) {
    constexpr double kA1 = 0.75487766624669276005;
    constexpr double kA2 = 0.56984029099805326591;
    const double index = static_cast<double>(n);
    const double u = 0.5 + kA1 * index;
    const double v = 0.5 + kA2 * index;
    return {static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v))};
}

}

void Fluid2DSolver::emit(Fluid2D& fluid, float dt) {
    for (FluidSource2D& source : fluid.sources) {
        if (!source.enabled || !std::isfinite(source.rate) || source.rate <= 0.0f || source.region.area() <= 0.0f ||
            !finite(source.region.center)) {
            continue;
        }
        const float owed = source.carry + source.rate * dt;
        const float whole = std::floor(owed);
        source.carry = owed - whole;
        const auto count = static_cast<std::uint64_t>(std::min(whole, 1.0e6f));
        const FluidRegion2D& region = source.region;
        for (std::uint64_t k = 0; k < count && fluid.count() < fluid.params.max_particles; ++k) {
            // Point `emitted` of the sequence: one per particle. Over a circle the square maps to the disc by area
            // (radius sqrt(u), angle 2 pi v), with the engine's deterministic trigonometry.
            const glm::vec2 u = r2_point(source.emitted);
            glm::vec2 position{0.0f};
            if (region.shape == FluidRegion2D::Shape::Circle) {
                const SinCos turn = det_sin_cos(2.0f * kPi * u.y);
                position = region.center + glm::vec2{turn.cos, turn.sin} * (region.radius * std::sqrt(u.x));
            } else {
                position = region.center + (u * 2.0f - glm::vec2{1.0f}) * region.half_extents;
            }
            if (!fluid.spawn(position, source.velocity)) {
                break;
            }
            ++source.emitted;
        }
        // At max_particles the rest of this tick's emission is dropped, not queued.
    }
}

void Fluid2DSolver::drain(Fluid2D& fluid) {
    bool any = false;
    for (const FluidDrain2D& drain : fluid.drains) {
        any = any || (drain.enabled && drain.region.area() > 0.0f);
    }
    if (!any) {
        return;
    }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < fluid.positions_.size(); ++i) {
        FluidDrain2D* taken = nullptr;
        for (FluidDrain2D& drain : fluid.drains) {
            if (drain.enabled && drain.region.contains(fluid.positions_[i])) {
                taken = &drain;
                break;
            }
        }
        if (taken != nullptr) {
            ++taken->drained;
            continue;
        }
        fluid.positions_[kept] = fluid.positions_[i];
        fluid.velocities_[kept] = fluid.velocities_[i];
        fluid.densities_[kept] = fluid.densities_[i];
        ++kept;
    }
    if (kept != fluid.positions_.size()) {
        fluid.positions_.resize(kept);
        fluid.velocities_.resize(kept);
        fluid.densities_.resize(kept);
        fluid.query_valid_ = false;
    }
}

void Fluid2DSolver::sense(Fluid2D& fluid, ecs::Entity entity, ecs::EventWriter<FluidSensorEvent2D>& events) {
    const float spacing = 2.0f * particle_radius(fluid.params);
    for (std::size_t k = 0; k < fluid.sensors.size(); ++k) {
        FluidSensor2D& sensor = fluid.sensors[k];
        const float area = sensor.region.area();
        if (area <= 0.0f) {
            continue;
        }
        const FluidQuery2D found = fluid.query(sensor.region);
        sensor.fill = static_cast<float>(found.count) * spacing * spacing / area;
        const bool was_full = sensor.full;
        if (!sensor.full && sensor.fill >= sensor.threshold) {
            sensor.full = true;
        } else if (sensor.full && sensor.fill < sensor.threshold - std::max(sensor.hysteresis, 0.0f)) {
            sensor.full = false;
        }
        if (sensor.full != was_full) {
            events.send(FluidSensorEvent2D{
                    .fluid = entity,
                    .sensor = static_cast<std::uint32_t>(k),
                    .full = sensor.full,
                    .fill = sensor.fill,
                    .count = found.count,
            });
        }
    }
}

namespace {

// The 2D solver's inverse inertia of a Dynamic body: its first non-trigger collider of box, circle, polygon (in that
// order) about the body origin, with the body's whole mass. 0 when it has none.
[[nodiscard]] float inverse_inertia_of(const ecs::World& world, ecs::Entity entity, float mass) {
    float inertia = 0.0f;
    if (const BoxCollider2D* box = world.try_get<BoxCollider2D>(entity); box != nullptr && !box->is_trigger) {
        inertia = inertia_2d_box(mass, box->size.x, box->size.y);
    } else if (const CircleCollider2D* circle = world.try_get<CircleCollider2D>(entity);
               circle != nullptr && !circle->is_trigger) {
        inertia = inertia_2d_circle(mass, circle->radius);
    } else if (const PolygonCollider2D* polygon = world.try_get<PolygonCollider2D>(entity);
               polygon != nullptr && !polygon->is_trigger) {
        inertia = inertia_2d_polygon(mass, polygon->vertices, polygon->count);
    }
    return inertia > 0.0f && std::isfinite(inertia) ? 1.0f / inertia : 0.0f;
}

// Gives each coupled body the fluids' reaction: velocity += impulse / mass, angular velocity += angular impulse /
// inertia. The reaction is explicit (the body did not move while the fluid was solved), so a body much lighter than the
// water that pushes it would overshoot: the change is capped so the body does not end faster, along the change, than
// the fastest particle that pushed it plus gravity over the tick, nor spinning faster than that speed over its longest
// contact arm (unless it already was). A floating body ends near rest and is never capped. A sleeping body stays
// asleep unless the change would wake it (the solver's sleep thresholds).
void apply_coupling(ecs::World& world, const Boundaries& boundaries, glm::vec2 gravity, float dt) {
    const float gravity_step = finite(gravity) ? std::sqrt(glm::dot(gravity, gravity)) * dt : 0.0f;
    for (const CouplingBody2D& coupled : boundaries.bodies) {
        if (coupled.impulse == glm::vec2{0.0f} && coupled.angular_impulse == 0.0f) {
            continue;
        }
        if (!finite(coupled.impulse) || !std::isfinite(coupled.angular_impulse)) {
            continue;
        }
        RigidBody2D* body = world.try_get<RigidBody2D>(coupled.entity);
        if (body == nullptr || body->type != BodyType::Dynamic) {
            continue;
        }
        const float mass = body->mass > kMinDynamicMass ? body->mass : kMinDynamicMass;
        glm::vec2 dv = coupled.impulse / mass;
        float dw = coupled.angular_impulse * inverse_inertia_of(world, coupled.entity, mass);
        const float cap = coupled.particle_speed + gravity_step;
        const float dv_length = std::sqrt(glm::dot(dv, dv));
        if (dv_length > 0.0f) {
            const glm::vec2 direction = dv / dv_length;
            const float along = glm::dot(glm::vec2{body->velocity.x, body->velocity.y}, direction) + dv_length;
            if (along > cap) {
                dv -= direction * std::min(along - cap, dv_length);
            }
        }
        if (coupled.arm > 0.0f && dw != 0.0f) {
            const float spin_cap = std::max(cap / coupled.arm, std::abs(body->angular_velocity));
            dw = std::clamp(body->angular_velocity + dw, -spin_cap, spin_cap) - body->angular_velocity;
        }
        if (body->sleeping) {
            if (glm::dot(dv, dv) < kSleepLinear * kSleepLinear && std::abs(dw) < kSleepAngular) {
                continue;
            }
            body->sleeping = false;
            if (PhysicsSleepState2D* sleep = world.try_ctx<PhysicsSleepState2D>()) {
                sleep->quiet.erase(coupled.entity);
            }
        }
        body->velocity.x += dv.x;
        body->velocity.y += dv.y;
        body->angular_velocity += dw;
    }
}

}

FluidNeighbors2D fluid_neighbors_2d(std::span<const glm::vec2> positions, float h, JobSystem* jobs) {
    Fluid2DScratch s;
    s.predicted.assign(positions.begin(), positions.end());
    Params p;
    p.h = h;
    p.inv_h = 1.0f / h;
    Fluid2DSolver::build_neighbors(p, positions.size(), jobs, s);
    return FluidNeighbors2D{.count = std::move(s.neighbor_count), .list = std::move(s.neighbors)};
}

bool Fluid2D::spawn(glm::vec2 position, glm::vec2 velocity) {
    if (positions_.size() >= params.max_particles || !finite(position) || !finite(velocity)) {
        return false;
    }
    positions_.push_back(position);
    velocities_.push_back(velocity);
    densities_.push_back(0.0f);
    query_valid_ = false;
    return true;
}

std::size_t Fluid2D::spawn_box(glm::vec2 min, glm::vec2 max, glm::vec2 velocity) {
    if (!finite(min) || !finite(max) || !finite(velocity)) {
        return 0;
    }
    const float r = particle_radius(params);
    const float spacing = 2.0f * r;
    // Whole lattice steps that fit, with a little slack so a box of exactly k spacings holds k.
    const float columns = std::floor((max.x - min.x) / spacing + 1.0e-3f);
    const float rows = std::floor((max.y - min.y) / spacing + 1.0e-3f);
    if (!(columns >= 1.0f) || !(rows >= 1.0f)) {
        return 0;
    }
    const auto nx = static_cast<std::size_t>(std::min(columns, 1.0e7f));
    const auto ny = static_cast<std::size_t>(std::min(rows, 1.0e7f));
    std::size_t added = 0;
    for (std::size_t j = 0; j < ny; ++j) {
        for (std::size_t i = 0; i < nx; ++i) {
            const glm::vec2 offset{spacing * static_cast<float>(i), spacing * static_cast<float>(j)};
            if (!spawn(min + glm::vec2{r} + offset, velocity)) {
                return added;
            }
            ++added;
        }
    }
    return added;
}

void Fluid2D::clear() {
    positions_.clear();
    velocities_.clear();
    densities_.clear();
    query_valid_ = false;
}

bool FluidRegion2D::contains(glm::vec2 point) const {
    const glm::vec2 d = point - center;
    if (shape == Shape::Circle) {
        return radius >= 0.0f && glm::dot(d, d) <= radius * radius;
    }
    return std::abs(d.x) <= half_extents.x && std::abs(d.y) <= half_extents.y;
}

float FluidRegion2D::area() const {
    if (shape == Shape::Circle) {
        return std::isfinite(radius) && radius > 0.0f ? kPi * radius * radius : 0.0f;
    }
    const bool valid = finite(half_extents) && half_extents.x > 0.0f && half_extents.y > 0.0f;
    return valid ? 4.0f * half_extents.x * half_extents.y : 0.0f;
}

void Fluid2D::build_query_grid() const {
    const std::size_t n = positions_.size();
    const float inv_cell = 0.5f / particle_radius(params);
    std::uint32_t table = 64;
    while (static_cast<std::size_t>(table) < 2 * n) {
        table *= 2;
    }
    const std::uint32_t mask = table - 1;
    query_cell_.resize(n);
    query_bucket_start_.assign(static_cast<std::size_t>(table) + 1, 0);
    query_sorted_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        query_cell_[i] = glm::ivec2{cell_coord(positions_[i].x * inv_cell), cell_coord(positions_[i].y * inv_cell)};
        ++query_bucket_start_[cell_hash(query_cell_[i], mask) + 1];
    }
    for (std::uint32_t b = 0; b < table; ++b) {
        query_bucket_start_[b + 1] += query_bucket_start_[b];
    }
    std::vector<std::uint32_t> fill(table, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t b = cell_hash(query_cell_[i], mask);
        query_sorted_[query_bucket_start_[b] + fill[b]++] = static_cast<std::uint32_t>(i);
    }
    query_valid_ = true;
}

// Calls `fn(i)` for every particle whose cell lies in the cell range of [lo, hi], each once, cell by cell. Falls back
// to every particle in order when the range has more cells than there are particles.
template <class F>
void Fluid2D::for_each_in_cells(glm::vec2 lo, glm::vec2 hi, F&& fn) const {
    const std::size_t n = positions_.size();
    if (n == 0 || !finite(lo) || !finite(hi) || lo.x > hi.x || lo.y > hi.y) {
        return;
    }
    if (!query_valid_) {
        build_query_grid();
    }
    const float inv_cell = 0.5f / particle_radius(params);
    const glm::ivec2 c0{cell_coord(lo.x * inv_cell), cell_coord(lo.y * inv_cell)};
    const glm::ivec2 c1{cell_coord(hi.x * inv_cell), cell_coord(hi.y * inv_cell)};
    const double cells = (static_cast<double>(c1.x) - c0.x + 1.0) * (static_cast<double>(c1.y) - c0.y + 1.0);
    if (cells > static_cast<double>(n)) {
        for (std::size_t i = 0; i < n; ++i) {
            fn(i);
        }
        return;
    }
    const auto mask = static_cast<std::uint32_t>(query_bucket_start_.size() - 2);
    for (int y = c0.y; y <= c1.y; ++y) {
        for (int x = c0.x; x <= c1.x; ++x) {
            const glm::ivec2 cell{x, y};
            const std::uint32_t b = cell_hash(cell, mask);
            for (std::uint32_t k = query_bucket_start_[b]; k < query_bucket_start_[b + 1]; ++k) {
                const std::uint32_t i = query_sorted_[k];
                if (query_cell_[i] == cell) {
                    fn(i);
                }
            }
        }
    }
}

FluidQuery2D Fluid2D::query(const FluidRegion2D& region) const {
    FluidQuery2D out;
    const glm::vec2 reach = region.shape == FluidRegion2D::Shape::Circle ? glm::vec2{region.radius}
                                                                         : region.half_extents;
    glm::vec2 sum{0.0f};
    for_each_in_cells(region.center - reach, region.center + reach, [&](std::size_t i) {
        if (region.contains(positions_[i])) {
            ++out.count;
            sum += velocities_[i];
        }
    });
    const float spacing = 2.0f * particle_radius(params);
    out.volume = static_cast<float>(out.count) * spacing * spacing;
    out.average_velocity = out.count > 0 ? sum / static_cast<float>(out.count) : glm::vec2{0.0f};
    return out;
}

FluidQuery2D Fluid2D::query(std::span<const glm::vec2> polygon) const {
    FluidQuery2D out;
    if (polygon.size() < 3) {
        return out;
    }
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{-std::numeric_limits<float>::max()};
    for (const glm::vec2 v : polygon) {
        if (!finite(v)) {
            return out;
        }
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
    glm::vec2 sum{0.0f};
    for_each_in_cells(lo, hi, [&](std::size_t i) {
        // Even-odd: count the edges a ray toward +X crosses.
        const glm::vec2 point = positions_[i];
        bool inside = false;
        for (std::size_t a = 0, b = polygon.size() - 1; a < polygon.size(); b = a++) {
            const glm::vec2 va = polygon[a];
            const glm::vec2 vb = polygon[b];
            if ((va.y > point.y) != (vb.y > point.y) &&
                point.x < (vb.x - va.x) * (point.y - va.y) / (vb.y - va.y) + va.x) {
                inside = !inside;
            }
        }
        if (inside) {
            ++out.count;
            sum += velocities_[i];
        }
    });
    const float spacing = 2.0f * particle_radius(params);
    out.volume = static_cast<float>(out.count) * spacing * spacing;
    out.average_velocity = out.count > 0 ? sum / static_cast<float>(out.count) : glm::vec2{0.0f};
    return out;
}

bool Fluid2D::is_wet(glm::vec2 point) const {
    const float reach = 2.0f * particle_radius(params);
    bool wet = false;
    for_each_in_cells(point - glm::vec2{reach}, point + glm::vec2{reach}, [&](std::size_t i) {
        const glm::vec2 d = positions_[i] - point;
        wet = wet || glm::dot(d, d) <= reach * reach;
    });
    return wet;
}

std::uint64_t Fluid2D::state_hash() const {
    StateHasher hasher;
    hasher.u64(positions_.size());
    for (std::size_t i = 0; i < positions_.size(); ++i) {
        hasher.vec2(positions_[i]);
        hasher.vec2(velocities_[i]);
    }
    hasher.u64(sources.size());
    for (const FluidSource2D& source : sources) {
        hasher.f32(source.carry);
        hasher.u64(source.emitted);
    }
    hasher.u64(sensors.size());
    for (const FluidSensor2D& sensor : sensors) {
        hasher.u8(sensor.full ? 1 : 0);
    }
    return hasher.value();
}

namespace {

// Fluids that are stepped together (LQ-62): two fluids join when their particles' boxes, grown by each one's kernel
// support, overlap and each one's mask takes the other's layer. Joined transitively, in view order (union-find on
// view indices, the group of the lower index wins), at most 32 to a group (a member bit each); a fluid that would
// make a 33rd starts its own group. Empty fluids stay alone.
[[nodiscard]] std::vector<FluidGroup2D> fluid_groups(ecs::World& world) {
    struct Entry {
        ecs::Entity entity{};
        Fluid2D* fluid = nullptr;
        glm::vec2 lo{0.0f};
        glm::vec2 hi{0.0f};
        bool empty = true;
    };
    std::vector<Entry> entries;
    auto view = world.view<Fluid2D>();
    for (ecs::Entity entity : view) {
        Fluid2D& fluid = view.get<Fluid2D>(entity);
        Entry entry{.entity = entity, .fluid = &fluid};
        if (fluid.count() > 0) {
            entry.empty = false;
            entry.lo = glm::vec2{std::numeric_limits<float>::max()};
            entry.hi = glm::vec2{-std::numeric_limits<float>::max()};
            for (const glm::vec2 position : fluid.positions()) {
                entry.lo = glm::min(entry.lo, position);
                entry.hi = glm::max(entry.hi, position);
            }
            const float h = resolve(fluid.params).h;
            entry.lo -= glm::vec2{h};
            entry.hi += glm::vec2{h};
        }
        entries.push_back(entry);
    }
    std::vector<std::size_t> root(entries.size());
    std::vector<std::size_t> size(entries.size(), 1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        root[i] = i;
    }
    const auto find = [&](std::size_t i) {
        while (root[i] != i) {
            i = root[i];
        }
        return i;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            const Entry& a = entries[i];
            const Entry& b = entries[j];
            if (a.empty || b.empty || a.hi.x < b.lo.x || b.hi.x < a.lo.x || a.hi.y < b.lo.y || b.hi.y < a.lo.y) {
                continue;
            }
            if ((a.fluid->params.mask & b.fluid->params.layer) == 0 || (b.fluid->params.mask & a.fluid->params.layer) == 0) {
                continue;
            }
            const std::size_t ra = find(i);
            const std::size_t rb = find(j);
            if (ra == rb || size[ra] + size[rb] > 32) {
                continue;
            }
            const std::size_t low = std::min(ra, rb);
            const std::size_t high = std::max(ra, rb);
            root[high] = low;
            size[low] += size[high];
        }
    }
    std::vector<FluidGroup2D> groups;
    std::vector<std::size_t> group_of(entries.size(), std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::size_t r = find(i);
        if (group_of[r] == std::numeric_limits<std::size_t>::max()) {
            group_of[r] = groups.size();
            groups.emplace_back();
        }
        FluidGroup2D& group = groups[group_of[r]];
        group.fluids.push_back(entries[i].fluid);
        group.entities.push_back(entries[i].entity);
    }
    return groups;
}

}

void run_fluid_2d(ecs::World& world, JobSystem* jobs) {
    auto view = world.view<Fluid2D>();
    bool any = false;
    for (ecs::Entity entity : view) {
        const Fluid2D& fluid = view.get<Fluid2D>(entity);
        if (fluid.count() > 0 || !fluid.sources.empty() || !fluid.sensors.empty()) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }
    const float dt = world.ctx<Time>().fixed_delta_time;
    if (dt > 0.0f && std::isfinite(dt)) {
        for (ecs::Entity entity : view) {
            Fluid2DSolver::emit(view.get<Fluid2D>(entity), dt);
        }
    }
    const glm::vec3 gravity = world.ctx<Physics2DConfig>().gravity;
    // Where any fluid can be this tick, for the static polygon and compound parts.
    glm::vec2 reach_lo{std::numeric_limits<float>::max()};
    glm::vec2 reach_hi{-std::numeric_limits<float>::max()};
    for (ecs::Entity entity : view) {
        glm::vec2 lo;
        glm::vec2 hi;
        if (fluid_reach(view.get<Fluid2D>(entity), glm::vec2{gravity.x, gravity.y}, dt, lo, hi)) {
            reach_lo = glm::min(reach_lo, lo);
            reach_hi = glm::max(reach_hi, hi);
        }
    }
    // run_physics_2d ends with update_world_transforms, so the collider matrices are this tick's.
    Boundaries boundaries = gather_boundaries(world, reach_lo, reach_hi);
    Fluid2DScratch& scratch = world.ctx<Fluid2DScratch>();
    ecs::EventWriter<FluidContactEvent2D> contacts{world};
    for (const FluidGroup2D& group : fluid_groups(world)) {
        Fluid2DSolver::step(group, boundaries, glm::vec2{gravity.x, gravity.y}, dt, jobs, scratch, contacts);
    }
    apply_coupling(world, boundaries, glm::vec2{gravity.x, gravity.y}, dt);
    ecs::EventWriter<FluidSensorEvent2D> events{world};
    for (ecs::Entity entity : view) {
        Fluid2D& fluid = view.get<Fluid2D>(entity);
        Fluid2DSolver::drain(fluid);
        Fluid2DSolver::sense(fluid, entity, events);
    }
}

void register_fluid_2d_system(ecs::World& world) {
    FluidInstall2D* install = world.try_ctx<FluidInstall2D>();
    if (install == nullptr || install->registered) {
        return;
    }
    install->registered = true;
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics,
                     [](ecs::World& w) { run_fluid_2d(w, w.ctx<FluidInstall2D>().jobs); });
}

void install_fluid_2d(ecs::World& world, JobSystem* jobs) {
    world.ctx<FluidInstall2D>().jobs = jobs;
    // Before the simulation systems exist (a game constructing its world), register_simulation_systems adds the
    // system after run_physics_3d, so it lands after both physics steps either way.
    const SimulationSystemsRegistered* simulation = world.try_ctx<SimulationSystemsRegistered>();
    if (simulation != nullptr && simulation->value) {
        register_fluid_2d_system(world);
    }
}

}
