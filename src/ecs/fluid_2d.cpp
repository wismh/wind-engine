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
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
// The solver's hot loops run four particles per SSE2 register (x64 baseline). Elsewhere (wasm, arm64) the scalar loops
// run; both give the same bits.
#define ENGINE_FLUID_SSE 1
#endif

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
constexpr std::size_t kGrain = 64;
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

// The world box of a region (a circle's square).
[[nodiscard]] std::pair<glm::vec2, glm::vec2> region_box(const FluidRegion2D& region) {
    const glm::vec2 reach = region.shape == FluidRegion2D::Shape::Circle ? glm::vec2{region.radius}
                                                                         : region.half_extents;
    return {region.center - reach, region.center + reach};
}

// A 64-bit key of words (integers and float bits), for the sleep marks: integer operations only, so the same on every
// target. Not a state hash; two different contents giving one key only miss a wake.
struct SleepKey {
    std::uint64_t value = 0x9E3779B97F4A7C15ull;

    void u32(std::uint32_t word) {
        value = (value ^ word) * 0xFF51AFD7ED558CCDull;
        value ^= value >> 32;
    }
    void f32(float word) {
        u32(std::bit_cast<std::uint32_t>(word));
    }
    void vec2(glm::vec2 word) {
        f32(word.x);
        f32(word.y);
    }
};

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
    // On a Dynamic or Kinematic body (or handed in by FluidStepOptions2D::movable).
    bool movable = false;
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
// Total order on shapes by their content (bit patterns), so every peer that holds the same colliders reads them in the
// same order whatever its entity order.
[[nodiscard]] bool shape_content_less(const ShapeBoundary& a, const ShapeBoundary& b) {
    const auto key = [](const ShapeBoundary& s) {
        std::array<std::uint32_t, 31> k{};
        std::size_t at = 0;
        k[at++] = static_cast<std::uint32_t>(s.kind);
        for (const float f : {s.lo.x, s.lo.y, s.hi.x, s.hi.y, s.frame.position.x, s.frame.position.y, s.frame.cos_a,
                              s.frame.sin_a, s.half.x, s.half.y, s.radius}) {
            k[at++] = std::bit_cast<std::uint32_t>(f);
        }
        k[at++] = static_cast<std::uint32_t>(s.count);
        for (int i = 0; i < s.count && at + 2 <= k.size() - 2; ++i) {
            k[at++] = std::bit_cast<std::uint32_t>(s.vertices[static_cast<std::size_t>(i)].x);
            k[at++] = std::bit_cast<std::uint32_t>(s.vertices[static_cast<std::size_t>(i)].y);
        }
        k[29] = s.layer;
        k[30] = s.mask;
        return k;
    };
    return key(a) < key(b);
}

[[nodiscard]] bool sdf_content_less(const SdfBoundary& a, const SdfBoundary& b) {
    const auto key = [](const SdfBoundary& s) {
        return std::array<std::uint32_t, 10>{
                std::bit_cast<std::uint32_t>(s.frame.position.x), std::bit_cast<std::uint32_t>(s.frame.position.y),
                std::bit_cast<std::uint32_t>(s.frame.cos_a),      std::bit_cast<std::uint32_t>(s.frame.sin_a),
                std::bit_cast<std::uint32_t>(s.field->origin().x), std::bit_cast<std::uint32_t>(s.field->origin().y),
                std::bit_cast<std::uint32_t>(s.field->cell_size()), std::bit_cast<std::uint32_t>(s.field->band()),
                s.layer, s.mask};
    };
    const auto ka = key(a);
    const auto kb = key(b);
    if (ka != kb) {
        return ka < kb;
    }
    return a.field->hash() < b.field->hash();
}

[[nodiscard]] ShapeBoundary shape_of(const FluidBoundary2D& in) {
    ShapeBoundary shape{.layer = in.layer, .mask = in.mask};
    shape.movable = true;
    if (in.kind == FluidBoundary2D::Kind::Polygon) {
        ColliderItem2D item{.shape = ColliderShape2D::Polygon, .layer = in.layer, .mask = in.mask};
        item.polygon_count = std::min<int>(in.count, kPolygonCollider2DCapacity);
        for (int i = 0; i < item.polygon_count; ++i) {
            item.polygon[i] = in.vertices[static_cast<std::size_t>(i)];
        }
        if (!polygon_boundary(item, shape)) {
            shape.count = 0;
        }
        shape.movable = true;
        return shape;
    }
    shape.frame = Frame2D{.position = in.position, .cos_a = in.axis.x, .sin_a = in.axis.y};
    if (in.kind == FluidBoundary2D::Kind::Circle) {
        shape.kind = ShapeBoundary::Kind::Circle;
        shape.radius = in.radius;
        shape.lo = in.position - glm::vec2{in.radius};
        shape.hi = in.position + glm::vec2{in.radius};
    } else {
        shape.kind = ShapeBoundary::Kind::Box;
        shape.half = in.half;
        const glm::vec2 extent{std::abs(in.axis.x) * in.half.x + std::abs(in.axis.y) * in.half.y,
                               std::abs(in.axis.y) * in.half.x + std::abs(in.axis.x) * in.half.y};
        shape.lo = in.position - extent;
        shape.hi = in.position + extent;
    }
    return shape;
}

[[nodiscard]] FluidBoundary2D boundary_of(const ShapeBoundary& shape) {
    FluidBoundary2D out{.layer = shape.layer, .mask = shape.mask};
    if (shape.kind == ShapeBoundary::Kind::Polygon) {
        out.kind = FluidBoundary2D::Kind::Polygon;
        out.count = static_cast<std::uint8_t>(shape.count);
        for (int i = 0; i < shape.count; ++i) {
            out.vertices[static_cast<std::size_t>(i)] = shape.vertices[static_cast<std::size_t>(i)];
        }
        return out;
    }
    out.kind = shape.kind == ShapeBoundary::Kind::Circle ? FluidBoundary2D::Kind::Circle : FluidBoundary2D::Kind::Box;
    out.position = shape.frame.position;
    out.axis = glm::vec2{shape.frame.cos_a, shape.frame.sin_a};
    out.half = shape.half;
    out.radius = shape.radius;
    return out;
}

Boundaries gather_boundaries(ecs::World& world, glm::vec2 reach_lo, glm::vec2 reach_hi,
                             const FluidStepOptions2D& options = {}) {
    Boundaries out;
    const bool world_movable = options.movable_from_world;
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
        shape.movable = under_movable_body(world, entity);
        if (shape.movable && !world_movable) {
            return;
        }
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
        shape.movable = under_movable_body(world, entity);
        if (shape.movable && !world_movable) {
            return;
        }
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
        if ((from_cache && !movable) || (movable && !world_movable)) {
            return;  // a static one is in the static cache, read below
        }
        ShapeBoundary shape;
        const glm::mat4 matrix = collider_world_matrix_2d(world, entity, transform);
        if (polygon_boundary(make_polygon_item_2d(entity, matrix, polygon, movable), shape)) {
            shape.movable = movable;
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
        if ((from_cache && !movable) || under_dynamic_body(world, entity) || (movable && !world_movable)) {
            return;
        }
        const glm::mat4 matrix = collider_world_matrix_2d(world, entity, transform);
        for (std::uint32_t part = 0; part < compound.shapes.size(); ++part) {
            ShapeBoundary shape;
            if (polygon_boundary(make_compound_item_2d(entity, matrix, compound, part, movable), shape)) {
                shape.movable = movable;
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
    for (const FluidBoundary2D& boundary : options.movable) {
        ShapeBoundary shape = shape_of(boundary);
        if (shape.kind != ShapeBoundary::Kind::Polygon || shape.count >= 3) {
            out.shapes.push_back(shape);
        }
    }
    // Content order, the same on every peer that holds the same colliders (net sync). Ties are equal shapes.
    std::stable_sort(out.shapes.begin(), out.shapes.end(), shape_content_less);
    std::stable_sort(out.sdf.begin(), out.sdf.end(), sdf_content_less);
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
    // Sleeping, in world units per second, and the ticks below `sleep_speed` before a particle sleeps.
    bool sleep = false;
    float sleep_speed = 0.0f;
    float wake_speed = 0.0f;
    std::uint32_t sleep_ticks = 60;
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
    p.sleep = fluid.sleep;
    p.sleep_speed = positive_or(fluid.sleep_speed, 0.5f) * p.radius;
    p.wake_speed = std::max(positive_or(fluid.wake_speed, 4.0f) * p.radius, p.sleep_speed);
    p.sleep_ticks = static_cast<std::uint32_t>(std::clamp(fluid.sleep_ticks, 1, 65535));
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

// The neighbour search's spatial hash: cells of h, a counting sort into a power-of-two table, so a bucket lists its
// particles in group order. The candidates' positions and cells are copied in bucket order so the search reads them in
// sequence.
struct FluidHash2D {
    std::uint32_t mask = 0;
    std::vector<std::uint32_t> bucket_start;
    std::vector<std::uint32_t> sorted;
    std::vector<glm::vec2> position;
    std::vector<glm::ivec2> cell;
};

// Per-step buffers, reused across ticks and fluids. Not part of any fluid's state.
struct Fluid2DScratch {
    // What `resting` and `sleeping_ids` were built from: the group's particle count, its kernel support's bits and each
    // member's `sleep_stamp_`. Equal keys mean the same sleepers at the same positions, so the hash is reused.
    std::vector<std::uint64_t> resting_key;
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
    // SDF clearance cache (LQ-55): where each particle's SDF fields were last sampled and how far it then was from
    // pushing distance (the smallest `distance - radius` over the fields, or the distance to a field's allocated box).
    // An interpolated field changes at most sqrt(2) times as fast as the point moves, so while the particle is closer
    // to that point than half its clearance no field can push it and the samples are skipped; the result is the same.
    // -1: sample. Reset every step, since fields change between ticks.
    std::vector<glm::vec2> sdf_anchor;
    std::vector<float> sdf_clearance;
    std::vector<float> sdf_probe;
    std::vector<char> sdf_skip;
    // Per selected shape and SDF field: the members it bounds (bit k for member k).
    std::vector<std::uint32_t> shape_members;
    std::vector<std::uint32_t> sdf_members;
    // Each particle's hash cell; the awake particles' hash (per substep) and the sleepers' (per step).
    std::vector<glm::ivec2> cell;
    FluidHash2D moving;
    FluidHash2D resting;
    std::vector<std::uint32_t> hash_fill;
    std::vector<std::uint32_t> sleeping_ids;
    std::vector<std::uint32_t> neighbor_count;
    std::vector<std::uint32_t> neighbors;
    // Per neighbour slot, this iteration: the poly6 weight and the spiky gradient (h units), at `weight_slot`.
    std::vector<float> kernel;
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
    // Sleeping: per particle of the group, asleep (1), awake (0) or held awake for this step (2), and its rest ticks.
    std::vector<std::uint8_t> asleep;
    std::vector<std::uint16_t> rest;
    // What the passes run over, by position: the awake particles [0, awake) in group order, then the sleepers within a
    // kernel support of an awake one this substep [awake, listed), in group order (their lambdas push the awake ones;
    // they keep their positions). Every particle, in order, when none sleeps. A sleeper beyond them is only read, as a
    // neighbour of a listed sleeper.
    std::vector<std::uint32_t> order;
    std::size_t awake = 0;
    std::size_t listed = 0;
    // A sleeper some awake particle lists this substep: set to 1 from any range, read after the pass.
    std::vector<std::uint8_t> touched;
    // A listed sleeper an awake neighbour moving faster than the wake speed touched in the last substep.
    std::vector<std::uint8_t> woken;
    // Where each particle started the step, for the rest and wake speeds.
    std::vector<glm::vec2> start;
};

// The fluids `run_fluid_2d` steps as one particle system this tick, in view order, with their entities.
struct FluidGroup2D {
    std::vector<Fluid2D*> fluids;
    std::vector<ecs::Entity> entities;
};

struct Fluid2DSolver {
    static void step(const FluidGroup2D& group, Boundaries& boundaries, glm::vec2 gravity, float dt, JobSystem* jobs,
                     Fluid2DScratch& s, ecs::EventWriter<FluidContactEvent2D>& contacts);
    // Neighbour lists of the listed particles over `s.predicted` [0, n): the awake ones, then the sleepers they touch.
    static void build_neighbors(const Params& p, std::size_t n, JobSystem* jobs, Fluid2DScratch& s);
    // The hash of particles `ids` [0, count) at their predicted positions, and the lists of `s.order` positions
    // [first, last) from the awake particles' hash and the sleepers'.
    static void hash_into(const Params& p, const std::uint32_t* ids, std::size_t count, JobSystem* jobs,
                          Fluid2DScratch& s, FluidHash2D& out);
    static void search_neighbors(const Params& p, std::size_t begin, std::size_t end, JobSystem* jobs,
                                 Fluid2DScratch& s);
    // Sleeping, before a step: wakes what changed around the fluid's sleepers (gravity or parameters, a static collider
    // or an SDF chunk), holds what is near a movable collider or a source that emitted (`emitted`, per source) awake,
    // and keeps the marks of what the sleepers see.
    static void wake_before_step(Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 gravity,
                                 std::span<const std::uint8_t> emitted);
    // The marks of what bounds `fluid` within [lo, hi]: a key and world box per static shape and per allocated SDF
    // chunk, plus one per SDF field that is solid outside (a box over everything).
    static void sleep_marks(const Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 lo, glm::vec2 hi,
                            std::vector<Fluid2D::SleepMark>& out);
    // Calls `fn(i)` for each particle of `fluid` whose position lies in [lo, hi].
    template <class F>
    static void each_in_box(const Fluid2D& fluid, glm::vec2 lo, glm::vec2 hi, F&& fn);
    // A fresh `sleep_stamp_`: the particle arrays or the set of sleepers changed.
    static void restamp(Fluid2D& fluid);
    static void select_boundaries(const FluidGroup2D& group, const Params& p, const Boundaries& boundaries,
                                  std::size_t n, Fluid2DScratch& s);
    static void sweep_range(std::size_t begin, std::size_t end, Fluid2DScratch& s);
    // Sources, before the step: emit what each enabled source owes for `dt`. `emitted[k]` is 1 when source k emitted.
    static void emit(Fluid2D& fluid, float dt, std::vector<std::uint8_t>& emitted);
    // Drains, after the step: remove the particles that ended inside an enabled drain, keeping the others' order. A
    // drain that removed one wakes the sleepers within a kernel support of it.
    static void drain(Fluid2D& fluid);
    // Sensors, after the drains: measure each fill and send an event when one turns full or stops being full.
    static void sense(Fluid2D& fluid, ecs::Entity entity, ecs::EventWriter<FluidSensorEvent2D>& events);
    // Two-way coupling: `shape` moved particle i by `moved`, leaving it at `position` with contact normal `normal`.
    static void record_push(std::size_t i, const ShapeBoundary& shape, glm::vec2 moved,
                            glm::vec2 position, glm::vec2 normal, Fluid2DScratch& s);
    static void project_range(std::size_t begin, std::size_t end, Fluid2DScratch& s);
    // Sleeping, after the substeps: wakes the listed sleepers a fast awake neighbour touched, and counts the awake
    // particles' rest ticks, putting to sleep those at `sleep_ticks`. `active` is the awake count (`s.awake`). Speeds
    // are the step's displacement over `dt` (from `s.start`): a particle's velocity in a resting pool is the noise of
    // its last substep's boundary response, its displacement is not.
    static void settle(std::size_t active, float dt, JobSystem* jobs, Fluid2DScratch& s);
    // The density constraint's lambda (and density) of particles [begin, end), keeping each neighbour's kernel
    // gradient and weight for the delta pass.
    static void lambda_range(std::size_t begin, std::size_t end, const Params& p, bool mixed, Fluid2DScratch& s);
    // The position correction (s.delta) of particles [begin, end) from the lambdas and the kept kernel terms.
    static void delta_range(std::size_t begin, std::size_t end, const Params& p, bool mixed, Fluid2DScratch& s);
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

namespace {

// Tests run the scalar kernels on x64 too (`set_fluid_scalar_kernels_2d`), to compare their bits with the SIMD ones.
bool g_scalar_kernels = false;

// Where particle i keeps neighbour slot k's kernel terms: the poly6 weight, the spiky gradient's x and y, and one
// unused float, so the SIMD pass writes a slot with one 16-byte store.
[[nodiscard]] std::size_t weight_slot(std::size_t i, std::size_t k) {
    return (i * kMaxNeighbors + k) * 4;
}

// Lambda of particle i from its neighbour sums (the scalar tail of `lambda_range`, shared by both paths).
void finish_lambda(std::size_t i, float rho, glm::vec2 grad_i, float sum_sq, bool mixed, Fluid2DScratch& s) {
    const Params& own = s.members[s.owner[i]];
    const float inv_rest = s.inv_rest[i];
    const float inv_mass = 1.0f / s.mass[i];
    // Both sums in units of rest density: grad C = grad W / rho_0.
    const float own_share = mixed ? glm::dot(grad_i, grad_i) * inv_mass : glm::dot(grad_i, grad_i);
    sum_sq = (sum_sq + own_share) * (inv_rest * inv_rest);
    const float ratio = rho * inv_rest;
    s.density[i] = ratio;
    const float c = ratio - 1.0f;
    s.lambda[i] = c > 0.0f ? -c / (sum_sq + own.relaxation) : 0.0f;
}

void lambda_one(std::size_t i, const Params& p, bool mixed, Fluid2DScratch& s) {
    const glm::vec2 pi = s.predicted[i];
    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
    float rho = kPoly6;
    glm::vec2 grad_i{0.0f};
    float sum_sq = 0.0f;
    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
        const std::uint32_t j = nb[k];
        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
        const float r2 = glm::dot(d, d);
        const float w = poly6(r2);
        const glm::vec2 grad = spiky_grad(d, std::sqrt(r2));
        const std::size_t at = weight_slot(i, k);
        s.kernel[at] = w;
        s.kernel[at + 1] = grad.x;
        s.kernel[at + 2] = grad.y;
        rho += w;
        grad_i += grad;
        // Each particle's share of the correction is its inverse mass (heaviest 1): a neighbour's gradient counts
        // by its weight. 1 in a lone fluid.
        sum_sq += mixed ? glm::dot(grad, grad) / s.mass[j] : glm::dot(grad, grad);
    }
    finish_lambda(i, rho, grad_i, sum_sq, mixed, s);
}

// Delta of particle i from its neighbour sum (the scalar tail of `delta_range`).
void finish_delta(std::size_t i, glm::vec2 sum, const Params& p, bool mixed, Fluid2DScratch& s) {
    const Params& own = s.members[s.owner[i]];
    const float inv_rest = s.inv_rest[i];
    const float inv_mass = 1.0f / s.mass[i];
    // i takes its inverse mass's share of every correction: a lighter particle yields to a heavier one, so the
    // heavier sinks through it (buoyancy). 1 in a lone fluid.
    glm::vec2 delta = mixed ? sum * (inv_rest * p.h * inv_mass) : sum * (inv_rest * p.h);
    const float len2 = glm::dot(delta, delta);
    if (len2 > own.max_delta * own.max_delta) {
        delta *= own.max_delta / std::sqrt(len2);
    }
    s.delta[i] = delta;
}

void delta_one(std::size_t i, const Params& p, bool mixed, Fluid2DScratch& s) {
    const Params& own = s.members[s.owner[i]];
    const float li = s.lambda[i];
    const float inv_rest = s.inv_rest[i];
    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
    const float* slots = &s.kernel[weight_slot(i, 0)];
    const float* lambdas = s.lambda.data();
    const std::uint32_t count = s.neighbor_count[i];
    glm::vec2 sum{0.0f};
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t j = nb[k];
        const float* slot = slots + k * 4;
        const float w = slot[0] * own.inv_w_dq;
        const float w2 = w * w;
        const float scorr = -own.tensile * w2 * w2;
        // The neighbour's constraint in units of its own rest density. 1 in a lone fluid.
        const float rr = mixed ? s.inv_rest[j] / inv_rest : 1.0f;
        sum += (li + lambdas[j] * rr + scorr) * glm::vec2{slot[1], slot[2]};
    }
    finish_delta(i, sum, p, mixed, s);
}

#if defined(ENGINE_FLUID_SSE)
// a where `mask` is set, otherwise b.
[[nodiscard]] __m128 select(__m128 mask, __m128 a, __m128 b) {
    return _mm_or_ps(_mm_and_ps(mask, a), _mm_andnot_ps(mask, b));
}

// The x and y of four positions.
void gather_xy(const glm::vec2* at, const std::uint32_t (&j)[4], __m128& x, __m128& y) {
    const __m128 a = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const double*>(&at[j[0]])));
    const __m128 b = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const double*>(&at[j[1]])));
    const __m128 c = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const double*>(&at[j[2]])));
    const __m128 d = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const double*>(&at[j[3]])));
    const __m128 ab = _mm_unpacklo_ps(a, b);  // x0 x1 y0 y1
    const __m128 cd = _mm_unpacklo_ps(c, d);  // x2 x3 y2 y3
    x = _mm_movelh_ps(ab, cd);
    y = _mm_movehl_ps(cd, ab);
}

[[nodiscard]] __m128 gather(const float* at, const std::uint32_t (&j)[4]) {
    return _mm_setr_ps(at[j[0]], at[j[1]], at[j[2]], at[j[3]]);
}

// The four particles at `s.order` positions [k0, k0 + 4) (k0 a multiple of 4): their indices (a lane past `end` takes
// lane 0's), neighbour lists, counts (0 past `end`), positions, and the longest count. `build_neighbors` pads every list
// of a group to the longest with the particle itself, so slots up to it are valid indices.
struct Group4 {
    std::uint32_t own[4];
    const std::uint32_t* lists[4];
    __m128i count;
    __m128 x;
    __m128 y;
    std::uint32_t most = 0;
    int valid = 0;
};

[[nodiscard]] Group4 group_of(std::size_t k0, std::size_t end, const Fluid2DScratch& s, const glm::vec2* positions) {
    Group4 g{};
    alignas(16) std::int32_t counts[4];
    for (int l = 0; l < 4; ++l) {
        const std::size_t k = k0 + static_cast<std::size_t>(l);
        const bool valid = k < end;
        g.own[l] = s.order[valid ? k : k0];
        g.lists[l] = &s.neighbors[static_cast<std::size_t>(g.own[l]) * kMaxNeighbors];
        const std::uint32_t count = valid ? s.neighbor_count[g.own[l]] : 0;
        counts[l] = static_cast<std::int32_t>(count);
        g.most = std::max(g.most, count);
        g.valid += valid ? 1 : 0;
    }
    g.count = _mm_load_si128(reinterpret_cast<const __m128i*>(counts));
    gather_xy(positions, g.own, g.x, g.y);
    return g;
}

[[nodiscard]] __m128 active_at(std::uint32_t k, __m128i count) {
    return _mm_castsi128_ps(_mm_cmplt_epi32(_mm_set1_epi32(static_cast<std::int32_t>(k)), count));
}

// Four particles at once, one per lane. Every lane runs `lambda_one`'s operations in its order (same products, sums
// and comparisons; SSE never contracts a multiply and an add), so a lane's result is the scalar one, bit for bit.
// A lane past its particle's neighbour count keeps its sums.
template <bool mixed>
void lambda_four(std::size_t k0, std::size_t end, const Params& p, Fluid2DScratch& s) {
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 inv_h = _mm_set1_ps(p.inv_h);
    const __m128 poly = _mm_set1_ps(kPoly6);
    const __m128 spiky = _mm_set1_ps(kSpikyGrad);
    const __m128 tiny = _mm_set1_ps(1.0e-6f);
    const Group4 g = group_of(k0, end, s, s.predicted.data());
    __m128 rho = poly;
    __m128 gx = _mm_setzero_ps();
    __m128 gy = _mm_setzero_ps();
    __m128 sum_sq = _mm_setzero_ps();
    float* kernel[4];
    for (int l = 0; l < 4; ++l) {
        kernel[l] = &s.kernel[weight_slot(g.own[l], 0)];
    }
    for (std::uint32_t k = 0; k < g.most; ++k) {
        const std::uint32_t j[4] = {g.lists[0][k], g.lists[1][k], g.lists[2][k], g.lists[3][k]};
        const __m128 active = active_at(k, g.count);
        __m128 qx;
        __m128 qy;
        gather_xy(s.predicted.data(), j, qx, qy);
        const __m128 dx = _mm_mul_ps(_mm_sub_ps(g.x, qx), inv_h);
        const __m128 dy = _mm_mul_ps(_mm_sub_ps(g.y, qy), inv_h);
        const __m128 r2 = _mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy));
        // poly6: ((k t) t) t, 0 where r2 >= 1.
        const __m128 t = _mm_sub_ps(one, r2);
        const __m128 w = _mm_andnot_ps(_mm_cmpge_ps(r2, one), _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(poly, t), t), t));
        // spiky_grad: d ((k u) u / r), 0 where r <= 1e-6 or r >= 1.
        const __m128 r = _mm_sqrt_ps(r2);
        const __m128 u = _mm_sub_ps(one, r);
        const __m128 f = _mm_div_ps(_mm_mul_ps(_mm_mul_ps(spiky, u), u), r);
        const __m128 none = _mm_or_ps(_mm_cmple_ps(r, tiny), _mm_cmpge_ps(r, one));
        const __m128 grx = _mm_andnot_ps(none, _mm_mul_ps(dx, f));
        const __m128 gry = _mm_andnot_ps(none, _mm_mul_ps(dy, f));
        // Each lane's (w, gx, gy, 0) slot; slots past a lane's count are padding (written, never read). A lane past
        // `n` writes lane 0's slots before lane 0 does.
        __m128 row0 = w;
        __m128 row1 = grx;
        __m128 row2 = gry;
        __m128 row3 = _mm_setzero_ps();
        _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
        _mm_storeu_ps(kernel[3] + k * 4, row3);
        _mm_storeu_ps(kernel[2] + k * 4, row2);
        _mm_storeu_ps(kernel[1] + k * 4, row1);
        _mm_storeu_ps(kernel[0] + k * 4, row0);
        rho = select(active, _mm_add_ps(rho, w), rho);
        gx = select(active, _mm_add_ps(gx, grx), gx);
        gy = select(active, _mm_add_ps(gy, gry), gy);
        __m128 square = _mm_add_ps(_mm_mul_ps(grx, grx), _mm_mul_ps(gry, gry));
        if constexpr (mixed) {
            square = _mm_div_ps(square, gather(s.mass.data(), j));
        }
        sum_sq = select(active, _mm_add_ps(sum_sq, square), sum_sq);
    }
    alignas(16) float rho_out[4];
    alignas(16) float gx_out[4];
    alignas(16) float gy_out[4];
    alignas(16) float sum_out[4];
    _mm_store_ps(rho_out, rho);
    _mm_store_ps(gx_out, gx);
    _mm_store_ps(gy_out, gy);
    _mm_store_ps(sum_out, sum_sq);
    for (int l = 0; l < g.valid; ++l) {
        finish_lambda(g.own[l], rho_out[l], glm::vec2{gx_out[l], gy_out[l]}, sum_out[l], mixed, s);
    }
}

#endif

}

void Fluid2DSolver::lambda_range(std::size_t begin, std::size_t end, const Params& p, bool mixed, Fluid2DScratch& s) {
#if defined(ENGINE_FLUID_SSE)
    // Ranges start at multiples of kGrain (a multiple of 4); only the last group of the last range is partial.
    if (!g_scalar_kernels) {
        for (std::size_t k = begin; k < end; k += 4) {
            if (mixed) {
                lambda_four<true>(k, end, p, s);
            } else {
                lambda_four<false>(k, end, p, s);
            }
        }
        return;
    }
#endif
    for (std::size_t k = begin; k < end; ++k) {
        lambda_one(s.order[k], p, mixed, s);
    }
}

void Fluid2DSolver::delta_range(std::size_t begin, std::size_t end, const Params& p, bool mixed, Fluid2DScratch& s) {
    // Scalar on every target: the pass is mostly a gather of the neighbours' lambdas, which SSE2 loads one lane at a
    // time; four lanes measured no faster than this loop.
    for (std::size_t k = begin; k < end; ++k) {
        delta_one(s.order[k], p, mixed, s);
    }
}

void Fluid2DSolver::build_neighbors(const Params& p, std::size_t n, JobSystem* jobs, Fluid2DScratch& s) {
    // The awake particles at their predicted positions; the sleepers' hash is built once per step (they do not move).
    hash_into(p, s.order.data(), s.awake, jobs, s, s.moving);
    s.neighbor_count.resize(n);
    s.neighbors.resize(n * kMaxNeighbors);
    search_neighbors(p, 0, s.awake, jobs, s);
    if (s.awake < n) {
        // The sleepers the awake particles list, in group order, then their own lists: their lambdas push the awake.
        s.order.resize(s.awake);
        for (std::size_t i = 0; i < n; ++i) {
            if (s.touched[i] != 0) {
                s.touched[i] = 0;
                s.order.push_back(static_cast<std::uint32_t>(i));
            }
        }
        s.listed = s.order.size();
        search_neighbors(p, s.awake, s.listed, jobs, s);
    } else {
        s.listed = n;
    }
    // The SIMD passes run four particles in step to the longest of their lists: pad the shorter ones with the particle
    // itself (a slot past the count is never summed).
    for_ranges(jobs, s.listed, [&](std::size_t begin, std::size_t end) {
        for (std::size_t k0 = begin; k0 < end; k0 += 4) {
            const std::size_t stop = std::min(k0 + 4, end);
            std::uint32_t most = 0;
            for (std::size_t k = k0; k < stop; ++k) {
                most = std::max(most, s.neighbor_count[s.order[k]]);
            }
            for (std::size_t k = k0; k < stop; ++k) {
                const std::uint32_t i = s.order[k];
                for (std::uint32_t slot = s.neighbor_count[i]; slot < most; ++slot) {
                    s.neighbors[static_cast<std::size_t>(i) * kMaxNeighbors + slot] = i;
                }
            }
        }
    });
}

void Fluid2DSolver::hash_into(const Params& p, const std::uint32_t* ids, std::size_t count, JobSystem* jobs,
                              Fluid2DScratch& s, FluidHash2D& out) {
    std::uint32_t table = 64;
    while (static_cast<std::size_t>(table) < 2 * count) {
        table *= 2;
    }
    const std::uint32_t mask = table - 1;
    out.mask = mask;
    s.cell.resize(s.predicted.size());
    out.bucket_start.assign(static_cast<std::size_t>(table) + 1, 0);
    out.sorted.resize(count);
    for_ranges(jobs, count, [&](std::size_t begin, std::size_t end) {
        for (std::size_t k = begin; k < end; ++k) {
            const std::uint32_t i = ids[k];
            const glm::vec2 q = s.predicted[i] * p.inv_h;
            s.cell[i] = glm::ivec2{cell_coord(q.x), cell_coord(q.y)};
        }
    });
    for (std::size_t k = 0; k < count; ++k) {
        ++out.bucket_start[cell_hash(s.cell[ids[k]], mask) + 1];
    }
    for (std::uint32_t b = 0; b < table; ++b) {
        out.bucket_start[b + 1] += out.bucket_start[b];
    }
    // Counting sort, stable: a bucket lists its particles in `ids` order (group order).
    std::vector<std::uint32_t>& fill = s.hash_fill;
    fill.assign(table, 0);
    out.position.resize(count);
    out.cell.resize(count);
    for (std::size_t k = 0; k < count; ++k) {
        const std::uint32_t i = ids[k];
        const std::uint32_t b = cell_hash(s.cell[i], mask);
        const std::uint32_t at = out.bucket_start[b] + fill[b]++;
        out.sorted[at] = i;
        out.position[at] = s.predicted[i];
        out.cell[at] = s.cell[i];
    }
}

namespace {

// Appends to `out` [count, kMaxNeighbors) the particles of `hash` in the 3 x 3 cells around `ci` closer than h to `pi`
// (`h2` = h^2), other than `i`, cell by cell (dy, then dx), each bucket in its order. Returns the new count. The hash's
// arrays are read through local pointers: the list is written through `out`, which the compiler cannot otherwise tell
// apart from them.
[[nodiscard]] std::uint32_t scan_hash(const FluidHash2D& hash, std::size_t i, glm::vec2 pi, glm::ivec2 ci, float h2,
                                      std::uint32_t* out, std::uint32_t count) {
    const std::uint32_t* const bucket_start = hash.bucket_start.data();
    const std::uint32_t* const sorted = hash.sorted.data();
    const glm::vec2* const position = hash.position.data();
    const glm::ivec2* const cell = hash.cell.data();
    const std::uint32_t mask = hash.mask;
    for (int dy = -1; dy <= 1 && count < kMaxNeighbors; ++dy) {
        for (int dx = -1; dx <= 1 && count < kMaxNeighbors; ++dx) {
            const glm::ivec2 c{ci.x + dx, ci.y + dy};
            const std::uint32_t b = cell_hash(c, mask);
            // Branch-free: every candidate is written at `count`, which moves on only when it is a neighbour (the
            // test fails about two times in three, unpredictably). Same list, same order.
            const std::uint32_t stop = bucket_start[b + 1];
            for (std::uint32_t k = bucket_start[b]; k < stop; ++k) {
                const std::uint32_t j = sorted[k];
                const glm::vec2 d = pi - position[k];
                const glm::ivec2 ck = cell[k];
                const bool take = (ck.x == c.x) & (ck.y == c.y) & (j != i) & (glm::dot(d, d) < h2);
                out[count] = j;
                count += take ? 1u : 0u;
                if (count == kMaxNeighbors) {
                    break;
                }
            }
        }
    }
    return count;
}

}

void Fluid2DSolver::search_neighbors(const Params& p, std::size_t first, std::size_t last, JobSystem* jobs,
                                     Fluid2DScratch& s) {
    const float h2 = p.h * p.h;
    const bool sleepers = s.awake < s.predicted.size();
    const bool marking = sleepers && first < s.awake;
    for_ranges(jobs, last - first, [&](std::size_t begin, std::size_t end) {
        for (std::size_t at = first + begin; at < first + end; ++at) {
            const std::size_t i = s.order[at];
            const glm::vec2 pi = s.predicted[i];
            const glm::ivec2 ci = s.cell[i];
            std::uint32_t* out = &s.neighbors[i * kMaxNeighbors];
            // The awake particles' hash, then the sleepers' (none when every particle is awake: the lists are then
            // those of a single hash).
            std::uint32_t count = scan_hash(s.moving, i, pi, ci, h2, out, 0);
            if (sleepers && count < kMaxNeighbors) {
                const std::uint32_t awake_count = count;
                count = scan_hash(s.resting, i, pi, ci, h2, out, count);
                if (marking) {
                    // The sleepers this awake particle lists. Several ranges may set one; each sets it to 1, so the
                    // set does not depend on the order they run in.
                    for (std::uint32_t slot = awake_count; slot < count; ++slot) {
                        std::atomic_ref<std::uint8_t>(s.touched[out[slot]]).store(1, std::memory_order_relaxed);
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
    // The substep starts too: a swept step meets every boundary between its start and its end. Only the awake
    // particles meet boundaries; sleepers keep their positions.
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint32_t i = s.order[k];
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
    for (std::size_t at = begin; at < end; ++at) {
        const std::size_t i = s.order[at];
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
    for (std::size_t at = begin; at < end && !s.shapes.empty(); ++at) {
        const std::size_t i = s.order[at];
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
    if (s.sdf.empty()) {
        return;
    }
    // Which particles may skip their samples this pass; the others start a new probe of their clearance.
    for (std::size_t at = begin; at < end; ++at) {
        const std::size_t i = s.order[at];
        const glm::vec2 moved = s.predicted[i] - s.sdf_anchor[i];
        const float clearance = s.sdf_clearance[i];
        const bool skip = clearance > 0.0f && 4.0f * glm::dot(moved, moved) < clearance * clearance;
        s.sdf_skip[i] = skip ? 1 : 0;
        s.sdf_probe[i] = std::numeric_limits<float>::max();
    }
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
            for (std::size_t at = batch; at < batch_end; ++at) {
                const std::size_t i = s.order[at];
                if ((members & (1u << s.owner[i])) == 0 || s.sdf_skip[i] != 0) {
                    continue;
                }
                const float radius = std::min(s.radius[i], band_radius);
                const glm::vec2 l = sdf->frame.to_local(s.predicted[i]);
                if (!solid_outside && (l.x < bounds->min.x - radius || l.x > bounds->max.x + radius ||
                                       l.y < bounds->min.y - radius || l.y > bounds->max.y + radius)) {
                    // Outside the field's box: the clearance is at least the way to that box.
                    const glm::vec2 outside{std::max({bounds->min.x - l.x, l.x - bounds->max.x, 0.0f}),
                                            std::max({bounds->min.y - l.y, l.y - bounds->max.y, 0.0f})};
                    s.sdf_probe[i] = std::min(s.sdf_probe[i], std::sqrt(glm::dot(outside, outside)) - radius);
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
                    s.sdf_probe[i] = std::min(s.sdf_probe[i], d - radius);
                    continue;
                }
                s.sdf_probe[i] = -1.0f;  // pushed (or in contact): sample again next time
                const glm::vec2 g = field.gradient(local[k]);
                if (g.x == 0.0f && g.y == 0.0f) {
                    continue;
                }
                s.predicted[i] = sdf->frame.to_world(local[k] + g * (radius - d));
                s.normal[i] = sdf->frame.rotate_to_world(g);
            }
        }
    }
    // A particle that was sampled and pushed by none keeps its new clearance from where it is now.
    for (std::size_t at = begin; at < end; ++at) {
        const std::size_t i = s.order[at];
        if (s.sdf_skip[i] != 0) {
            continue;
        }
        const float probe = s.sdf_probe[i];
        if (probe > 0.0f && probe < std::numeric_limits<float>::max()) {
            s.sdf_anchor[i] = s.predicted[i];
            s.sdf_clearance[i] = probe;
        } else {
            s.sdf_clearance[i] = -1.0f;
        }
    }
}

void Fluid2DSolver::step(const FluidGroup2D& group, Boundaries& boundaries, glm::vec2 gravity, float dt,
                         JobSystem* jobs, Fluid2DScratch& s, ecs::EventWriter<FluidContactEvent2D>& contacts) {
    std::size_t n = 0;
    std::size_t awake = 0;
    for (Fluid2D* fluid : group.fluids) {
        fluid->densities_.resize(fluid->positions_.size(), 0.0f);
        fluid->rest_ticks_.resize(fluid->positions_.size(), 0);
        fluid->asleep_.resize(fluid->positions_.size(), 0);
        n += fluid->positions_.size();
        for (const std::uint8_t state : fluid->asleep_) {
            awake += state != 1 ? 1 : 0;
        }
    }
    if (n == 0 || !(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }
    if (awake == 0) {
        return;  // every particle sleeps: nothing moves, the query grid stays valid
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
    s.asleep.resize(n);
    s.rest.resize(n);
    s.order.resize(n);
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
                s.asleep[at] = fluid.asleep_[i];
                s.rest[at] = fluid.rest_ticks_[i];
            }
        }
    }
    // The awake particles in group order; the passes run over them (and, for lambdas, over the sleepers they touch).
    s.awake = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (s.asleep[i] != 1) {
            s.order[s.awake++] = static_cast<std::uint32_t>(i);
        }
    }
    s.start.resize(n);
    std::copy(s.x.begin(), s.x.end(), s.start.begin());
    s.listed = s.awake;
    const bool sleepers = s.awake < n;
    if (sleepers) {
        s.touched.assign(n, 0);
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
    s.sdf_anchor.assign(n, glm::vec2{0.0f});
    s.sdf_clearance.assign(n, -1.0f);
    s.sdf_probe.resize(n);
    s.sdf_skip.resize(n);
    s.boundaries = &boundaries;
    if (sleepers) {
        // A sleeper keeps its position and a zero velocity through the step: its predicted position is where it is,
        // and an awake neighbour's viscosity and curl read its zero velocity. Its hash is built once for the step, or
        // kept from an earlier step while the same sleepers sit at the same positions (a cache: the same hash).
        std::vector<std::uint64_t> key{n, std::bit_cast<std::uint32_t>(p.h)};
        bool stamped = true;
        for (const Fluid2D* fluid : group.fluids) {
            key.push_back(fluid->sleep_stamp_);
            stamped = stamped && fluid->sleep_stamp_ != 0;
        }
        const bool cached = stamped && key == s.resting_key;
        if (!cached) {
            s.sleeping_ids.clear();
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (s.asleep[i] == 1) {
                v[i] = glm::vec2{0.0f};
                s.predicted[i] = x[i];
                s.from[i] = x[i];
                s.velocity[i] = glm::vec2{0.0f};
                s.normal[i] = glm::vec2{0.0f};
                if (!cached) {
                    s.sleeping_ids.push_back(static_cast<std::uint32_t>(i));
                }
            }
        }
        if (cached) {
            // The awake particles' hash recomputes only their own cells; the sleepers' are those the hash was built on.
            s.cell.resize(n);
            for (std::size_t k = 0; k < s.resting.sorted.size(); ++k) {
                s.cell[s.resting.sorted[k]] = s.resting.cell[k];
            }
        } else {
            hash_into(p, s.sleeping_ids.data(), s.sleeping_ids.size(), jobs, s, s.resting);
            s.resting_key = stamped ? std::move(key) : std::vector<std::uint64_t>{};
        }
    }
    // A particle's mass in world units: its fluid's density times the area of one lattice cell (the rest spacing
    // squared). The reaction of a push `moved` over a substep is an impulse of mass * moved / sub_dt on the body.
    const bool any_mass = std::any_of(member_mass.begin(), member_mass.end(), [](float m) { return m > 0.0f; });

    const std::size_t active = s.awake;
    for (int sub = 0; sub < p.substeps; ++sub) {
        for_ranges(jobs, active, [&](std::size_t begin, std::size_t end) {
            for (std::size_t at = begin; at < end; ++at) {
                const std::size_t i = s.order[at];
                v[i] += member_gravity[s.owner[i]] * sub_dt;
                s.pre_velocity[i] = v[i];
                s.predicted[i] = x[i] + v[i] * sub_dt;
                s.from[i] = x[i];
                s.normal[i] = glm::vec2{0.0f};
            }
        });
        build_neighbors(p, n, jobs, s);
        s.kernel.resize(n * kMaxNeighbors * 4);
        select_boundaries(group, p, boundaries, active, s);
        s.coupled = any_mass &&
                    std::any_of(s.shapes.begin(), s.shapes.end(), [](const ShapeBoundary* shape) { return shape->body >= 0; });
        if (s.coupled) {
            s.pushes.assign(n * kCouplingSlots, Fluid2DScratch::Push{});
        }

        for (int iteration = 0; iteration < p.iterations; ++iteration) {
            // Lambda: the density constraint C_i = rho_i / rho_0 - 1 >= 0 (only compression pushes). Keeps each
            // neighbour's kernel gradient and weight for the delta pass, which reads the same positions.
            // The listed sleepers too: their lambdas push the awake particles, which they never move.
            for_ranges(jobs, s.listed,
                       [&](std::size_t begin, std::size_t end) { lambda_range(begin, end, p, mixed, s); });
            // Delta: gathered from the neighbours' lambdas, with the s_corr tensile term.
            for_ranges(jobs, active, [&](std::size_t begin, std::size_t end) {
                delta_range(begin, end, p, mixed, s);
                // Apply, sweep and project the same range in the same pass: the delta pass reads no positions,
                // only the lambdas and the kept kernel terms, so moving these particles here races with nothing.
                for (std::size_t at = begin; at < end; ++at) {
                    const std::size_t i = s.order[at];
                    s.predicted[i] += s.delta[i];
                }
                sweep_range(begin, end, s);
                project_range(begin, end, s);
                for (std::size_t at = begin; at < end; ++at) {
                    const std::size_t i = s.order[at];
                    s.from[i] = s.predicted[i];
                }
            });
        }

        // Velocity from the solved positions, boundary response, then XSPH viscosity (gathered from the
        // neighbours' velocities into a second buffer).
        for_ranges(jobs, active, [&](std::size_t begin, std::size_t end) {
            for (std::size_t at = begin; at < end; ++at) {
                const std::size_t i = s.order[at];
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
            for_ranges(jobs, s.listed, [&](std::size_t begin, std::size_t end) {
                for (std::size_t at = begin; at < end; ++at) {
                    const std::size_t i = s.order[at];
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
        for_ranges(jobs, active, [&](std::size_t begin, std::size_t end) {
            for (std::size_t at = begin; at < end; ++at) {
                const std::size_t i = s.order[at];
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
            for (std::size_t at = 0; at < active; ++at) {
                const std::size_t i = s.order[at];
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
    // fluids and at least one of which is awake, counted once (lower index first when both are awake), summed serially
    // in particle order per pair of fluids.
    if (mixed) {
        struct Contact {
            std::size_t count = 0;
            glm::vec2 sum{0.0f};
            glm::vec2 lo{std::numeric_limits<float>::max()};
            glm::vec2 hi{-std::numeric_limits<float>::max()};
        };
        const std::size_t m = s.members.size();
        std::vector<Contact> pairs(m * m);
        for (std::size_t at = 0; at < active; ++at) {
            const std::size_t i = s.order[at];
            const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
            for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                const std::uint32_t j = nb[k];
                if ((j <= i && s.asleep[j] != 1) || s.owner[j] == s.owner[i]) {
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

    if (std::any_of(s.members.begin(), s.members.end(), [](const Params& member) { return member.sleep; })) {
        settle(active, dt, jobs, s);
    }

    // Scatter the particles back, in member order.
    std::size_t at = 0;
    for (Fluid2D* fluid : group.fluids) {
        bool changed = false;
        for (std::size_t i = 0; i < fluid->positions_.size(); ++i, ++at) {
            fluid->positions_[i] = s.x[at];
            fluid->velocities_[i] = s.v[at];
            fluid->densities_[i] = s.density[at];
            fluid->rest_ticks_[i] = s.rest[at];
            changed = changed || (fluid->asleep_[i] == 1) != (s.asleep[at] == 1);
            fluid->asleep_[i] = s.asleep[at];
        }
        if (changed) {
            restamp(*fluid);
        }
    }
}

void Fluid2DSolver::settle(std::size_t active, float dt, JobSystem* jobs, Fluid2DScratch& s) {
    // Woken: a listed sleeper that an awake neighbour, moving faster than the sleeper's wake speed, touched in the
    // last substep.
    s.woken.assign(s.listed - active, 0);
    for_ranges(jobs, s.listed - active, [&](std::size_t begin, std::size_t end) {
        for (std::size_t at = active + begin; at < active + end; ++at) {
            const std::size_t i = s.order[at];
            const float wake = s.members[s.owner[i]].wake_speed * dt;
            const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
            for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                const std::uint32_t j = nb[k];
                const glm::vec2 moved = s.x[j] - s.start[j];
                if (s.asleep[j] != 1 && glm::dot(moved, moved) > wake * wake) {
                    s.woken[at - active] = 1;
                    break;
                }
            }
        }
    });
    // Rest: an awake particle below its sleep speed counts a tick; at `sleep_ticks` it falls asleep with a zero
    // velocity. A held one (near a movable collider or a source) and one of a fluid without sleep count nothing.
    for_ranges(jobs, active, [&](std::size_t begin, std::size_t end) {
        for (std::size_t at = begin; at < end; ++at) {
            const std::size_t i = s.order[at];
            const Params& own = s.members[s.owner[i]];
            if (!own.sleep || s.asleep[i] == 2) {
                s.asleep[i] = 0;
                s.rest[i] = 0;
                continue;
            }
            const glm::vec2 moved = s.x[i] - s.start[i];
            const float still = own.sleep_speed * dt;
            if (glm::dot(moved, moved) >= still * still) {
                s.rest[i] = 0;
                continue;
            }
            s.rest[i] = static_cast<std::uint16_t>(std::min<std::uint32_t>(s.rest[i] + 1u, 65535u));
            if (s.rest[i] >= own.sleep_ticks) {
                s.asleep[i] = 1;
                s.v[i] = glm::vec2{0.0f};
            }
        }
    });
    for (std::size_t at = active; at < s.listed; ++at) {
        if (s.woken[at - active] != 0) {
            const std::size_t i = s.order[at];
            s.asleep[i] = 0;
            s.rest[i] = 0;
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

void Fluid2DSolver::emit(Fluid2D& fluid, float dt, std::vector<std::uint8_t>& emitted) {
    emitted.assign(fluid.sources.size(), 0);
    for (std::size_t index = 0; index < fluid.sources.size(); ++index) {
        FluidSource2D& source = fluid.sources[index];
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
            emitted[index] = 1;
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
    fluid.rest_ticks_.resize(fluid.positions_.size(), 0);
    fluid.asleep_.resize(fluid.positions_.size(), 0);
    std::vector<std::uint8_t> removed(fluid.drains.size(), 0);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < fluid.positions_.size(); ++i) {
        std::size_t taken = fluid.drains.size();
        for (std::size_t k = 0; k < fluid.drains.size(); ++k) {
            if (fluid.drains[k].enabled && fluid.drains[k].region.contains(fluid.positions_[i])) {
                taken = k;
                break;
            }
        }
        if (taken < fluid.drains.size()) {
            ++fluid.drains[taken].drained;
            removed[taken] = 1;
            continue;
        }
        fluid.positions_[kept] = fluid.positions_[i];
        fluid.velocities_[kept] = fluid.velocities_[i];
        fluid.densities_[kept] = fluid.densities_[i];
        fluid.rest_ticks_[kept] = fluid.rest_ticks_[i];
        fluid.asleep_[kept] = fluid.asleep_[i];
        ++kept;
    }
    if (kept != fluid.positions_.size()) {
        fluid.positions_.resize(kept);
        fluid.velocities_.resize(kept);
        fluid.densities_.resize(kept);
        fluid.rest_ticks_.resize(kept);
        fluid.asleep_.resize(kept);
        fluid.query_valid_ = false;
        restamp(fluid);
        // A hole in a sleeping pool: what sleeps around a drain that took a particle wakes and flows in.
        if (fluid.params.sleep) {
            const float reach = resolve(fluid.params).h + particle_radius(fluid.params);
            for (std::size_t k = 0; k < fluid.drains.size(); ++k) {
                if (removed[k] != 0) {
                    const auto [lo, hi] = region_box(fluid.drains[k].region);
                    (void)fluid.wake_in_box(lo - glm::vec2{reach}, hi + glm::vec2{reach});
                }
            }
        }
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

// The last `sleep_stamp_` handed out; stamps are never reused, so one names one content of one fluid.
std::atomic<std::uint64_t> g_sleep_stamp{0};

}

void Fluid2DSolver::restamp(Fluid2D& fluid) {
    fluid.sleep_stamp_ = g_sleep_stamp.fetch_add(1, std::memory_order_relaxed) + 1;
}

template <class F>
void Fluid2DSolver::each_in_box(const Fluid2D& fluid, glm::vec2 lo, glm::vec2 hi, F&& fn) {
    fluid.for_each_in_cells(lo, hi, [&](std::size_t i) {
        const glm::vec2 q = fluid.positions_[i];
        if (q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y) {
            fn(i);
        }
    });
}

namespace {

// A box over everything, finite so it can grow.
constexpr float kFarBox = 1.0e30f;
// Samples per side of an SDF block the sleepers keep a mark of (a divisor of kSdfChunkCells, even).
constexpr int kSdfSleepBlock = 8;

[[nodiscard]] bool boxes_overlap(glm::vec2 lo_a, glm::vec2 hi_a, glm::vec2 lo_b, glm::vec2 hi_b) {
    return !(hi_a.x < lo_b.x || lo_a.x > hi_b.x || hi_a.y < lo_b.y || lo_a.y > hi_b.y);
}

[[nodiscard]] std::uint64_t shape_key(const ShapeBoundary& shape) {
    SleepKey key;
    key.u32(static_cast<std::uint32_t>(shape.kind));
    key.vec2(shape.lo);
    key.vec2(shape.hi);
    key.vec2(shape.frame.position);
    key.f32(shape.frame.cos_a);
    key.f32(shape.frame.sin_a);
    key.vec2(shape.half);
    key.f32(shape.radius);
    key.u32(static_cast<std::uint32_t>(shape.count));
    for (int v = 0; v < shape.count; ++v) {
        key.vec2(shape.vertices[static_cast<std::size_t>(v)]);
    }
    key.u32(shape.layer);
    key.u32(shape.mask);
    return key.value;
}

// The chunk index range a sample-index coordinate range covers, clamped to what an int holds.
[[nodiscard]] int chunk_index(float sample) {
    const float chunk = std::floor(sample / static_cast<float>(kSdfChunkCells));
    return std::isfinite(chunk) ? static_cast<int>(std::clamp(chunk, -1.0e8f, 1.0e8f)) : 0;
}

// Gravity and the resolved parameters a sleeper rests under: a change wakes the whole fluid.
[[nodiscard]] std::uint64_t sleep_env_key(const Fluid2D& fluid, glm::vec2 gravity) {
    const Params p = resolve(fluid.params);
    SleepKey key;
    for (const float value : {p.radius, p.h, p.rest_density, p.viscosity, p.relaxation, p.tensile, p.friction,
                              p.restitution, p.density, p.cohesion, p.vorticity, p.sleep_speed, p.wake_speed}) {
        key.f32(value);
    }
    key.u32(static_cast<std::uint32_t>(p.iterations));
    key.u32(static_cast<std::uint32_t>(p.substeps));
    key.u32(p.sleep_ticks);
    key.u32(fluid.params.layer);
    key.u32(fluid.params.mask);
    const float scale = std::isfinite(fluid.params.gravity_scale) ? fluid.params.gravity_scale : 0.0f;
    key.vec2(finite(gravity) ? gravity * scale : glm::vec2{0.0f});
    return key.value;
}

// A mark's key and box by their bits, for sorting and comparing marks.
[[nodiscard]] std::array<std::uint64_t, 3> mark_bits(std::uint64_t key, glm::vec2 lo, glm::vec2 hi) {
    const auto pair = [](glm::vec2 v) {
        return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(v.x)) << 32) | std::bit_cast<std::uint32_t>(v.y);
    };
    return {key, pair(lo), pair(hi)};
}

}

void Fluid2DSolver::sleep_marks(const Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 lo, glm::vec2 hi,
                                std::vector<Fluid2D::SleepMark>& out) {
    out.clear();
    // Static shapes only: a shape on a movable body holds what is near it awake every step instead.
    for (const ShapeBoundary& shape : boundaries.shapes) {
        if (shape.movable || !layers_match(fluid.params, shape.layer, shape.mask) ||
            !boxes_overlap(shape.lo, shape.hi, lo, hi)) {
            continue;
        }
        out.push_back(Fluid2D::SleepMark{.key = shape_key(shape), .lo = shape.lo, .hi = shape.hi});
    }
    for (const SdfBoundary& sdf : boundaries.sdf) {
        if (!layers_match(fluid.params, sdf.layer, sdf.mask)) {
            continue;
        }
        const SdfField2D& field = *sdf.field;
        SleepKey field_key;
        field_key.vec2(sdf.frame.position);
        field_key.f32(sdf.frame.cos_a);
        field_key.f32(sdf.frame.sin_a);
        field_key.vec2(field.origin());
        field_key.f32(field.cell_size());
        field_key.f32(field.band());
        field_key.u32(static_cast<std::uint32_t>(field.outside()));
        field_key.u32(sdf.layer);
        field_key.u32(sdf.mask);
        if (field.outside() == SdfOutside::Solid) {
            // Unallocated space is solid everywhere: a change of the field itself wakes every sleeper.
            out.push_back(Fluid2D::SleepMark{.key = field_key.value, .lo = glm::vec2{-kFarBox},
                                             .hi = glm::vec2{kFarBox}});
        }
        if (field.chunk_count() == 0) {
            continue;
        }
        // [lo, hi] in sample indices of the field, then the chunks around it (a chunk's cells reach one sample past
        // its own on each side).
        glm::vec2 local_lo{std::numeric_limits<float>::max()};
        glm::vec2 local_hi{-std::numeric_limits<float>::max()};
        for (const glm::vec2 corner : {lo, glm::vec2{hi.x, lo.y}, hi, glm::vec2{lo.x, hi.y}}) {
            const glm::vec2 l = (sdf.frame.to_local(corner) - field.origin()) / field.cell_size();
            local_lo = glm::min(local_lo, l);
            local_hi = glm::max(local_hi, l);
        }
        const glm::ivec2 c0{chunk_index(local_lo.x - 1.0f), chunk_index(local_lo.y - 1.0f)};
        const glm::ivec2 c1{chunk_index(local_hi.x + 1.0f), chunk_index(local_hi.y + 1.0f)};
        const double range = (static_cast<double>(c1.x) - c0.x + 1.0) * (static_cast<double>(c1.y) - c0.y + 1.0);
        std::vector<glm::ivec2> chunks;
        if (range > static_cast<double>(field.chunk_count())) {
            for (const glm::ivec2 chunk : field.chunk_coords()) {
                if (chunk.x >= c0.x && chunk.x <= c1.x && chunk.y >= c0.y && chunk.y <= c1.y) {
                    chunks.push_back(chunk);
                }
            }
        } else {
            for (int y = c0.y; y <= c1.y; ++y) {
                for (int x = c0.x; x <= c1.x; ++x) {
                    if (field.has_chunk({x, y})) {
                        chunks.push_back({x, y});
                    }
                }
            }
        }
        for (const glm::ivec2 chunk : chunks) {
            const std::span<const std::int16_t> samples = field.chunk_samples(chunk);
            // A mark per block of kSdfSleepBlock^2 samples, so an edit wakes what is near it, not the whole chunk.
            for (int by = 0; by < kSdfChunkCells; by += kSdfSleepBlock) {
                for (int bx = 0; bx < kSdfChunkCells; bx += kSdfSleepBlock) {
                    // The block's cells (a sample reaches the cells on both sides of it), in world XY.
                    const glm::ivec2 first = chunk * kSdfChunkCells + glm::ivec2{bx, by};
                    const glm::vec2 a = field.origin() + field.cell_size() * (glm::vec2{first} - 1.0f);
                    const glm::vec2 b =
                            field.origin() + field.cell_size() * (glm::vec2{first} + static_cast<float>(kSdfSleepBlock) + 1.0f);
                    glm::vec2 world_lo{std::numeric_limits<float>::max()};
                    glm::vec2 world_hi{-std::numeric_limits<float>::max()};
                    for (const glm::vec2 corner : {a, glm::vec2{b.x, a.y}, b, glm::vec2{a.x, b.y}}) {
                        const glm::vec2 w = sdf.frame.to_world(corner);
                        world_lo = glm::min(world_lo, w);
                        world_hi = glm::max(world_hi, w);
                    }
                    if (!boxes_overlap(world_lo, world_hi, lo, hi)) {
                        continue;
                    }
                    SleepKey key = field_key;
                    key.u32(static_cast<std::uint32_t>(first.x));
                    key.u32(static_cast<std::uint32_t>(first.y));
                    for (int y = by; y < by + kSdfSleepBlock; ++y) {
                        const std::int16_t* row = &samples[static_cast<std::size_t>(y * kSdfChunkCells + bx)];
                        for (int x = 0; x < kSdfSleepBlock; x += 2) {
                            key.u32(static_cast<std::uint32_t>(static_cast<std::uint16_t>(row[x])) |
                                    (static_cast<std::uint32_t>(static_cast<std::uint16_t>(row[x + 1])) << 16));
                        }
                    }
                    out.push_back(Fluid2D::SleepMark{.key = key.value, .lo = world_lo, .hi = world_hi});
                }
            }
        }
    }
}

void Fluid2DSolver::wake_before_step(Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 gravity,
                                     std::span<const std::uint8_t> emitted) {
    const std::size_t n = fluid.positions_.size();
    fluid.rest_ticks_.resize(n, 0);
    fluid.asleep_.resize(n, 0);
    if (!fluid.params.sleep || n == 0) {
        // Sleeping off (or nothing to sleep): every particle is awake and nothing is kept.
        (void)fluid.wake();
        fluid.sleep_marks_.clear();
        fluid.sleep_marks_valid_ = false;
        return;
    }
    const Params p = resolve(fluid.params);
    const float reach = p.h + p.radius;
    const glm::vec2 grow{reach};
    const auto count_asleep = [&] {
        return static_cast<std::size_t>(std::count(fluid.asleep_.begin(), fluid.asleep_.end(), std::uint8_t{1}));
    };
    std::size_t asleep = count_asleep();

    // Gravity or a parameter changed under the sleepers: they all wake.
    const std::uint64_t env = sleep_env_key(fluid, gravity);
    if (asleep > 0 && fluid.sleep_marks_valid_ && env != fluid.sleep_env_) {
        (void)fluid.wake();
        asleep = 0;
    }
    fluid.sleep_env_ = env;

    // Held awake for this step, sleepers included: what is near a collider on a movable body (it may move into the
    // fluid, and a body at rest in the fluid keeps being pushed) and near a source that emitted.
    bool held_sleeper = false;
    const auto hold = [&](glm::vec2 lo, glm::vec2 hi) {
        each_in_box(fluid, lo - grow, hi + grow, [&](std::size_t i) {
            held_sleeper = held_sleeper || fluid.asleep_[i] == 1;
            fluid.asleep_[i] = 2;
            fluid.rest_ticks_[i] = 0;
        });
    };
    for (std::size_t k = 0; k < fluid.sources.size() && k < emitted.size(); ++k) {
        if (emitted[k] != 0) {
            const auto [lo, hi] = region_box(fluid.sources[k].region);
            hold(lo, hi);
        }
    }
    for (const ShapeBoundary& shape : boundaries.shapes) {
        if (shape.movable && layers_match(fluid.params, shape.layer, shape.mask)) {
            hold(shape.lo, shape.hi);
        }
    }
    if (held_sleeper) {
        restamp(fluid);
    }
    asleep = count_asleep();

    // The marks of the boundaries are kept while a particle sleeps or can fall asleep at the end of this step, so the
    // next step finds what changed since the sleepers last saw them.
    bool may_sleep = false;
    for (std::size_t i = 0; i < n && !may_sleep; ++i) {
        may_sleep = fluid.asleep_[i] == 0 && fluid.rest_ticks_[i] + 1u >= p.sleep_ticks;
    }
    if (asleep == 0 && !may_sleep) {
        fluid.sleep_marks_.clear();
        fluid.sleep_marks_valid_ = false;
        return;
    }
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{-std::numeric_limits<float>::max()};
    for (const glm::vec2 position : fluid.positions_) {
        lo = glm::min(lo, position);
        hi = glm::max(hi, position);
    }
    std::vector<Fluid2D::SleepMark> marks;
    sleep_marks(fluid, boundaries, lo - grow, hi + grow, marks);
    const auto mark_less = [](const Fluid2D::SleepMark& a, const Fluid2D::SleepMark& b) {
        return mark_bits(a.key, a.lo, a.hi) < mark_bits(b.key, b.lo, b.hi);
    };
    const auto mark_equal = [](const Fluid2D::SleepMark& a, const Fluid2D::SleepMark& b) {
        return mark_bits(a.key, a.lo, a.hi) == mark_bits(b.key, b.lo, b.hi);
    };
    if (asleep > 0 && fluid.sleep_marks_valid_) {
        std::vector<Fluid2D::SleepMark>& before = fluid.sleep_marks_;
        const bool same = before.size() == marks.size() &&
                          std::equal(before.begin(), before.end(), marks.begin(), mark_equal);
        if (!same) {
            // What appeared or went away (a static collider added, moved or removed, an SDF chunk edited): the
            // sleepers within a kernel support of it wake.
            std::vector<Fluid2D::SleepMark> now = marks;
            std::sort(before.begin(), before.end(), mark_less);
            std::sort(now.begin(), now.end(), mark_less);
            std::vector<Fluid2D::SleepMark> changed;
            std::set_symmetric_difference(before.begin(), before.end(), now.begin(), now.end(),
                                          std::back_inserter(changed), mark_less);
            for (const Fluid2D::SleepMark& mark : changed) {
                (void)fluid.wake_in_box(mark.lo - grow, mark.hi + grow);
            }
        }
    }
    fluid.sleep_marks_ = std::move(marks);
    fluid.sleep_marks_valid_ = true;
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

void set_fluid_scalar_kernels_2d(bool scalar) {
    g_scalar_kernels = scalar;
}

FluidNeighbors2D fluid_neighbors_2d(std::span<const glm::vec2> positions, float h, JobSystem* jobs) {
    Fluid2DScratch s;
    s.predicted.assign(positions.begin(), positions.end());
    s.order.resize(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        s.order[i] = static_cast<std::uint32_t>(i);
    }
    s.awake = positions.size();
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
    rest_ticks_.resize(positions_.size(), 0);
    asleep_.resize(positions_.size(), 0);
    positions_.push_back(position);
    velocities_.push_back(velocity);
    densities_.push_back(0.0f);
    rest_ticks_.push_back(0);
    asleep_.push_back(0);
    query_valid_ = false;
    Fluid2DSolver::restamp(*this);
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
    rest_ticks_.clear();
    asleep_.clear();
    sleep_marks_.clear();
    sleep_marks_valid_ = false;
    query_valid_ = false;
    Fluid2DSolver::restamp(*this);
}

std::size_t Fluid2D::sleeping_count() const {
    return static_cast<std::size_t>(std::count(asleep_.begin(), asleep_.end(), std::uint8_t{1}));
}

std::size_t Fluid2D::wake() {
    std::size_t woke = 0;
    for (std::size_t i = 0; i < asleep_.size(); ++i) {
        woke += asleep_[i] == 1 ? 1 : 0;
        asleep_[i] = 0;
        rest_ticks_[i] = 0;
    }
    if (woke > 0) {
        Fluid2DSolver::restamp(*this);
    }
    return woke;
}

std::size_t Fluid2D::wake(const FluidRegion2D& region) {
    const auto [lo, hi] = region_box(region);
    std::size_t woke = 0;
    for_each_in_cells(lo, hi, [&](std::size_t i) {
        if (i < asleep_.size() && region.contains(positions_[i])) {
            woke += asleep_[i] == 1 ? 1 : 0;
            asleep_[i] = 0;
            rest_ticks_[i] = 0;
        }
    });
    if (woke > 0) {
        Fluid2DSolver::restamp(*this);
    }
    return woke;
}

std::size_t Fluid2D::wake_in_box(glm::vec2 lo, glm::vec2 hi) {
    std::size_t woke = 0;
    Fluid2DSolver::each_in_box(*this, lo, hi, [&](std::size_t i) {
        if (asleep_[i] == 1) {
            asleep_[i] = 0;
            rest_ticks_[i] = 0;
            ++woke;
        }
    });
    if (woke > 0) {
        Fluid2DSolver::restamp(*this);
    }
    return woke;
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

namespace {

// The state's byte layout: magic, version, then the arrays. Every value little-endian; floats by their bits.
constexpr std::uint32_t kStateMagic = 0x31464C57u;  // "WLF1"
// 2 adds each particle's sleep state and the sleepers' boundary marks; 1 (no sleep) still loads, every particle awake.
constexpr std::uint32_t kStateVersion = 2;

class StateWriter {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(value);
    }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value));
        u32(static_cast<std::uint32_t>(value >> 32));
    }
    void f32(float value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }
    void vec2(glm::vec2 value) {
        f32(value.x);
        f32(value.y);
    }
    [[nodiscard]] std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class StateReader {
public:
    explicit StateReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool u8(std::uint8_t& value) {
        if (at_ + 1 > bytes_.size()) {
            return false;
        }
        value = bytes_[at_++];
        return true;
    }
    [[nodiscard]] bool u16(std::uint16_t& value) {
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        if (!u8(lo) || !u8(hi)) {
            return false;
        }
        value = static_cast<std::uint16_t>(lo | (hi << 8));
        return true;
    }
    [[nodiscard]] bool u32(std::uint32_t& value) {
        if (at_ + 4 > bytes_.size()) {
            return false;
        }
        value = 0;
        for (int k = 0; k < 4; ++k) {
            value |= static_cast<std::uint32_t>(bytes_[at_++]) << (8 * k);
        }
        return true;
    }
    [[nodiscard]] bool u64(std::uint64_t& value) {
        std::uint32_t lo = 0;
        std::uint32_t hi = 0;
        if (!u32(lo) || !u32(hi)) {
            return false;
        }
        value = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
        return true;
    }
    [[nodiscard]] bool f32(float& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    [[nodiscard]] bool vec2(glm::vec2& value) {
        return f32(value.x) && f32(value.y);
    }
    [[nodiscard]] std::size_t left() const {
        return bytes_.size() - at_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
};

}

std::vector<std::uint8_t> Fluid2D::save_state() const {
    StateWriter out;
    out.u32(kStateMagic);
    out.u32(kStateVersion);
    out.u64(positions_.size());
    for (std::size_t i = 0; i < positions_.size(); ++i) {
        out.vec2(positions_[i]);
        out.vec2(velocities_[i]);
        out.f32(i < densities_.size() ? densities_[i] : 0.0f);
        out.u16(i < rest_ticks_.size() ? rest_ticks_[i] : 0);
        out.u8(i < asleep_.size() && asleep_[i] == 1 ? 1 : 0);
    }
    out.u64(sources.size());
    for (const FluidSource2D& source : sources) {
        out.f32(source.carry);
        out.u64(source.emitted);
    }
    out.u64(drains.size());
    for (const FluidDrain2D& drain : drains) {
        out.u64(drain.drained);
    }
    out.u64(sensors.size());
    for (const FluidSensor2D& sensor : sensors) {
        out.f32(sensor.fill);
        out.u8(sensor.full ? 1 : 0);
    }
    out.u8(sleep_marks_valid_ ? 1 : 0);
    out.u64(sleep_env_);
    out.u64(sleep_marks_.size());
    for (const SleepMark& mark : sleep_marks_) {
        out.u64(mark.key);
        out.vec2(mark.lo);
        out.vec2(mark.hi);
    }
    return out.take();
}

bool Fluid2D::load_state(std::span<const std::uint8_t> bytes) {
    StateReader in{bytes};
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (!in.u32(magic) || !in.u32(version) || magic != kStateMagic || (version != 1 && version != kStateVersion)) {
        return false;
    }
    const bool sleeping = version >= 2;
    const std::size_t stride = sleeping ? 23 : 20;
    if (!in.u64(count) || count > params.max_particles || count > in.left() / stride) {
        return false;
    }
    std::vector<glm::vec2> positions(static_cast<std::size_t>(count));
    std::vector<glm::vec2> velocities(static_cast<std::size_t>(count));
    std::vector<float> densities(static_cast<std::size_t>(count));
    std::vector<std::uint16_t> rest(static_cast<std::size_t>(count), 0);
    std::vector<std::uint8_t> asleep(static_cast<std::size_t>(count), 0);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!in.vec2(positions[i]) || !in.vec2(velocities[i]) || !in.f32(densities[i])) {
            return false;
        }
        if (sleeping && (!in.u16(rest[i]) || !in.u8(asleep[i]) || asleep[i] > 1)) {
            return false;
        }
    }
    struct SourceState {
        float carry = 0.0f;
        std::uint64_t emitted = 0;
    };
    std::uint64_t source_count = 0;
    if (!in.u64(source_count) || source_count > in.left() / 12) {
        return false;
    }
    std::vector<SourceState> source_state(static_cast<std::size_t>(source_count));
    for (SourceState& state : source_state) {
        if (!in.f32(state.carry) || !in.u64(state.emitted)) {
            return false;
        }
    }
    std::uint64_t drain_count = 0;
    if (!in.u64(drain_count) || drain_count > in.left() / 8) {
        return false;
    }
    std::vector<std::uint64_t> drained(static_cast<std::size_t>(drain_count));
    for (std::uint64_t& value : drained) {
        if (!in.u64(value)) {
            return false;
        }
    }
    std::uint64_t sensor_count = 0;
    if (!in.u64(sensor_count) || sensor_count > in.left() / 5) {
        return false;
    }
    std::vector<std::pair<float, bool>> sensor_state(static_cast<std::size_t>(sensor_count));
    for (auto& [fill, full] : sensor_state) {
        std::uint8_t flag = 0;
        if (!in.f32(fill) || !in.u8(flag) || flag > 1) {
            return false;
        }
        full = flag != 0;
    }
    std::uint8_t marks_valid = 0;
    std::uint64_t env = 0;
    std::vector<SleepMark> marks;
    if (sleeping) {
        std::uint64_t mark_count = 0;
        if (!in.u8(marks_valid) || marks_valid > 1 || !in.u64(env) || !in.u64(mark_count) ||
            mark_count > in.left() / 24) {
            return false;
        }
        marks.resize(static_cast<std::size_t>(mark_count));
        for (SleepMark& mark : marks) {
            if (!in.u64(mark.key) || !in.vec2(mark.lo) || !in.vec2(mark.hi)) {
                return false;
            }
        }
    }
    if (in.left() != 0) {
        return false;
    }
    positions_ = std::move(positions);
    velocities_ = std::move(velocities);
    densities_ = std::move(densities);
    rest_ticks_ = std::move(rest);
    asleep_ = std::move(asleep);
    sleep_marks_ = std::move(marks);
    sleep_env_ = env;
    sleep_marks_valid_ = marks_valid != 0;
    query_valid_ = false;
    Fluid2DSolver::restamp(*this);
    if (source_state.size() == sources.size()) {
        for (std::size_t k = 0; k < sources.size(); ++k) {
            sources[k].carry = source_state[k].carry;
            sources[k].emitted = source_state[k].emitted;
        }
    }
    if (drained.size() == drains.size()) {
        for (std::size_t k = 0; k < drains.size(); ++k) {
            drains[k].drained = drained[k];
        }
    }
    if (sensor_state.size() == sensors.size()) {
        for (std::size_t k = 0; k < sensors.size(); ++k) {
            sensors[k].fill = sensor_state[k].first;
            sensors[k].full = sensor_state[k].second;
        }
    }
    return true;
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
    // Sleep state decides what moves next. Without `sleep` it is all zero and left out, so a fluid that never sleeps
    // hashes as before sleeping existed.
    if (params.sleep) {
        for (std::size_t i = 0; i < positions_.size(); ++i) {
            hasher.u32(i < rest_ticks_.size() ? rest_ticks_[i] : 0u);
            hasher.u8(i < asleep_.size() && asleep_[i] == 1 ? 1 : 0);
        }
        // The key of gravity and the parameters is left out, like the parameters themselves.
        hasher.u8(sleep_marks_valid_ ? 1 : 0);
        hasher.u64(sleep_marks_.size());
        for (const SleepMark& mark : sleep_marks_) {
            hasher.u64(mark.key);
            hasher.vec2(mark.lo);
            hasher.vec2(mark.hi);
        }
    }
    return hasher.value();
}

namespace {

// Fills every FluidBox2D not filled yet into the Fluid2D on its parent, in view order, and marks it filled. A box whose
// parent has no Fluid2D waits.
void fill_fluid_boxes(ecs::World& world) {
    std::vector<ecs::Entity> boxes;
    for (const ecs::Entity entity : world.view<FluidBox2D>()) {
        if (!world.get<FluidBox2D>(entity).filled) {
            boxes.push_back(entity);
        }
    }
    for (const ecs::Entity entity : boxes) {
        const Parent* parent = world.try_get<Parent>(entity);
        Fluid2D* fluid = parent != nullptr && world.valid(parent->entity) ? world.try_get<Fluid2D>(parent->entity)
                                                                           : nullptr;
        if (fluid == nullptr) {
            continue;
        }
        FluidBox2D& box = world.get<FluidBox2D>(entity);
        const glm::mat4 matrix = ecs::world_matrix(world, entity);
        const glm::vec2 center{matrix[3][0], matrix[3][1]};
        const glm::vec2 half = glm::abs(box.size) * 0.5f;
        (void)fluid->spawn_box(center - half, center + half, box.velocity);
        box.filled = true;
    }
}

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

std::vector<FluidBoundary2D> movable_fluid_boundaries_2d(ecs::World& world) {
    const glm::vec2 far{std::numeric_limits<float>::max()};
    const Boundaries boundaries = gather_boundaries(world, -far, far);
    std::vector<FluidBoundary2D> out;
    for (const ShapeBoundary& shape : boundaries.shapes) {
        if (shape.movable) {
            out.push_back(boundary_of(shape));
        }
    }
    return out;
}

void run_fluid_2d(ecs::World& world, JobSystem* jobs) {
    run_fluid_2d(world, jobs, FluidStepOptions2D{});
}

void run_fluid_2d(ecs::World& world, JobSystem* jobs, const FluidStepOptions2D& options) {
    fill_fluid_boxes(world);
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
    // Per fluid in view order, per source: whether it emitted this tick (sleepers near it are held awake).
    std::vector<std::vector<std::uint8_t>> emitted;
    for (ecs::Entity entity : view) {
        emitted.emplace_back();
        if (dt > 0.0f && std::isfinite(dt)) {
            Fluid2DSolver::emit(view.get<Fluid2D>(entity), dt, emitted.back());
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
    Boundaries boundaries = gather_boundaries(world, reach_lo, reach_hi, options);
    {
        std::size_t index = 0;
        for (ecs::Entity entity : view) {
            Fluid2DSolver::wake_before_step(view.get<Fluid2D>(entity), boundaries, glm::vec2{gravity.x, gravity.y},
                                            emitted[index++]);
        }
    }
    Fluid2DScratch& scratch = world.ctx<Fluid2DScratch>();
    ecs::EventWriter<FluidContactEvent2D> contacts{world};
    for (const FluidGroup2D& group : fluid_groups(world)) {
        Fluid2DSolver::step(group, boundaries, glm::vec2{gravity.x, gravity.y}, dt, jobs, scratch, contacts);
    }
    if (options.push_bodies) {
        apply_coupling(world, boundaries, glm::vec2{gravity.x, gravity.y}, dt);
    }
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
