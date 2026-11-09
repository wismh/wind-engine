#include <engine/ecs/fluid_2d.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/transform.h>

#include "fluid_2d_internal.h"

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

// Heading of the matrix X axis in XY, as the 2D solver reads an entity's yaw.
[[nodiscard]] float yaw_of(const glm::mat4& matrix) {
    const float x = matrix[0][0];
    const float y = matrix[0][1];
    if (std::abs(x) < 1.0e-6f && std::abs(y) < 1.0e-6f) {
        return 0.0f;
    }
    return std::atan2(y, x);
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

[[nodiscard]] Frame2D frame_of(const glm::mat4& matrix) {
    const float angle = yaw_of(matrix);
    return Frame2D{.position = {matrix[3][0], matrix[3][1]}, .cos_a = std::cos(angle), .sin_a = std::sin(angle)};
}

struct SdfBoundary {
    const SdfField2D* field = nullptr;
    Frame2D frame{};
    uint32_t layer = 0;
    uint32_t mask = 0;
};

struct ShapeBoundary {
    enum class Kind { Box, Circle };
    Kind kind = Kind::Box;
    Frame2D frame{};
    glm::vec2 half{0.0f};  // Box
    float radius = 0.0f;   // Circle
    uint32_t layer = 0;
    uint32_t mask = 0;
    // World AABB of the shape.
    glm::vec2 lo{0.0f};
    glm::vec2 hi{0.0f};
};

// Every collider that can bound a fluid this tick, in view order. `field` points into the SdfCollider2D pool for this
// call only: run_fluid_2d creates and destroys nothing.
struct Boundaries {
    std::vector<SdfBoundary> sdf;
    std::vector<ShapeBoundary> shapes;
};

Boundaries gather_boundaries(ecs::World& world) {
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
        out.shapes.push_back(shape);
    });
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
    return p;
}

}

// Per-step buffers, reused across ticks and fluids. Not part of any fluid's state.
struct Fluid2DScratch {
    std::vector<glm::vec2> predicted;
    std::vector<glm::vec2> pre_velocity;
    std::vector<glm::vec2> delta;
    std::vector<glm::vec2> normal;  // last boundary normal this substep; zero when none
    std::vector<glm::vec2> velocity;
    std::vector<float> lambda;
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
};

struct Fluid2DSolver {
    static void step(Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 gravity, float dt, JobSystem* jobs,
                     Fluid2DScratch& s);
    // Neighbour lists of `s.predicted` [0, n).
    static void build_neighbors(const Params& p, std::size_t n, JobSystem* jobs, Fluid2DScratch& s);
    static void select_boundaries(const Fluid2DParams& fluid, const Params& p, const Boundaries& boundaries,
                                  std::size_t n, Fluid2DScratch& s);
    static void project_range(const Params& p, std::size_t begin, std::size_t end, Fluid2DScratch& s);
};

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

void Fluid2DSolver::select_boundaries(const Fluid2DParams& fluid, const Params& p, const Boundaries& boundaries,
                                      std::size_t n, Fluid2DScratch& s) {
    s.sdf.clear();
    s.shapes.clear();
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{-std::numeric_limits<float>::max()};
    for (std::size_t i = 0; i < n; ++i) {
        lo = glm::min(lo, s.predicted[i]);
        hi = glm::max(hi, s.predicted[i]);
    }
    // Per iteration the solver moves a particle at most `max_delta`, and an SDF projection at most a radius.
    const float grow = p.radius + (p.max_delta + p.radius) * static_cast<float>(p.iterations);
    lo -= glm::vec2{grow};
    hi += glm::vec2{grow};
    for (const SdfBoundary& sdf : boundaries.sdf) {
        if (layers_match(fluid, sdf.layer, sdf.mask)) {
            s.sdf.push_back(&sdf);
        }
    }
    for (const ShapeBoundary& shape : boundaries.shapes) {
        if (layers_match(fluid, shape.layer, shape.mask) && shape.hi.x >= lo.x && shape.lo.x <= hi.x &&
            shape.hi.y >= lo.y && shape.lo.y <= hi.y) {
            s.shapes.push_back(&shape);
        }
    }
}

// Pushes predicted positions [begin, end) out of every selected boundary: boxes and circles, then SDF colliders in
// gather order, so a particle squeezed between a moving shape and the terrain ends outside the terrain (and is pushed
// out of the shape on a later iteration). A particle's result depends only on itself and the read-only boundaries.
void Fluid2DSolver::project_range(const Params& p, std::size_t begin, std::size_t end, Fluid2DScratch& s) {
    for (std::size_t i = begin; i < end && !s.shapes.empty(); ++i) {
        glm::vec2 pos = s.predicted[i];
        for (const ShapeBoundary* shape : s.shapes) {
            if (pos.x < shape->lo.x - p.radius || pos.x > shape->hi.x + p.radius || pos.y < shape->lo.y - p.radius ||
                pos.y > shape->hi.y + p.radius) {
                continue;
            }
            if (shape->kind == ShapeBoundary::Kind::Circle) {
                const glm::vec2 d = pos - shape->frame.position;
                const float reach = shape->radius + p.radius;
                const float dist2 = glm::dot(d, d);
                if (dist2 >= reach * reach) {
                    continue;
                }
                const float dist = std::sqrt(dist2);
                const glm::vec2 n = dist > 1.0e-6f ? d / dist : glm::vec2{0.0f, 1.0f};
                pos = shape->frame.position + n * reach;
                s.normal[i] = n;
            } else {
                const glm::vec2 l = shape->frame.to_local(pos);
                const glm::vec2 half = shape->half + glm::vec2{p.radius};
                const float px = half.x - std::abs(l.x);
                const float py = half.y - std::abs(l.y);
                if (px <= 0.0f || py <= 0.0f) {
                    continue;
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
            }
        }
        s.predicted[i] = pos;
    }
    std::array<glm::vec2, kSdfBatch> local{};
    std::array<float, kSdfBatch> distance{};
    std::array<std::uint32_t, kSdfBatch> index{};
    for (const SdfBoundary* sdf : s.sdf) {
        const SdfField2D& field = *sdf->field;
        const float radius = std::min(p.radius, field.band() * 0.999f);
        const std::optional<SdfBounds2D> bounds = field.allocated_bounds();
        const bool solid_outside = field.outside() == SdfOutside::Solid;
        if (!bounds && !solid_outside) {
            continue;
        }
        for (std::size_t batch = begin; batch < end; batch += kSdfBatch) {
            const std::size_t batch_end = std::min(end, batch + kSdfBatch);
            std::size_t count = 0;
            for (std::size_t i = batch; i < batch_end; ++i) {
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
                if (d >= radius) {
                    continue;
                }
                const glm::vec2 g = field.gradient(local[k]);
                if (g.x == 0.0f && g.y == 0.0f) {
                    continue;
                }
                const std::uint32_t i = index[k];
                s.predicted[i] = sdf->frame.to_world(local[k] + g * (radius - d));
                s.normal[i] = sdf->frame.rotate_to_world(g);
            }
        }
    }
}

void Fluid2DSolver::step(Fluid2D& fluid, const Boundaries& boundaries, glm::vec2 gravity, float dt, JobSystem* jobs,
                         Fluid2DScratch& s) {
    const std::size_t n = fluid.positions_.size();
    fluid.densities_.resize(n, 0.0f);
    if (n == 0 || !(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }
    const Params p = resolve(fluid.params);
    const float sub_dt = dt / static_cast<float>(p.substeps);
    const float inv_dt = 1.0f / sub_dt;
    const float scale = std::isfinite(fluid.params.gravity_scale) ? fluid.params.gravity_scale : 0.0f;
    const glm::vec2 g = finite(gravity) ? gravity * scale : glm::vec2{0.0f};
    std::vector<glm::vec2>& x = fluid.positions_;
    std::vector<glm::vec2>& v = fluid.velocities_;
    std::vector<float>& density = fluid.densities_;
    s.predicted.resize(n);
    s.pre_velocity.resize(n);
    s.delta.resize(n);
    s.normal.resize(n);
    s.velocity.resize(n);
    s.lambda.resize(n);

    for (int sub = 0; sub < p.substeps; ++sub) {
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                v[i] += g * sub_dt;
                s.pre_velocity[i] = v[i];
                s.predicted[i] = x[i] + v[i] * sub_dt;
                s.normal[i] = glm::vec2{0.0f};
            }
        });
        build_neighbors(p, n, jobs, s);
        s.gradient.resize(n * kMaxNeighbors);
        s.weight.resize(n * kMaxNeighbors);
        select_boundaries(fluid.params, p, boundaries, n, s);

        for (int iteration = 0; iteration < p.iterations; ++iteration) {
            // Lambda: the density constraint C_i = rho_i / rho_0 - 1 >= 0 (only compression pushes). Keeps each
            // neighbour's kernel gradient and weight for the delta pass, which reads the same positions.
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const glm::vec2 pi = s.predicted[i];
                    const std::size_t base = i * kMaxNeighbors;
                    const std::uint32_t* nb = &s.neighbors[base];
                    glm::vec2* grads = &s.gradient[base];
                    float* weights = &s.weight[base];
                    float rho = kPoly6;
                    glm::vec2 grad_i{0.0f};
                    float sum_sq = 0.0f;
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const glm::vec2 d = (pi - s.predicted[nb[k]]) * p.inv_h;
                        const float r2 = glm::dot(d, d);
                        const float w = poly6(r2);
                        const glm::vec2 grad = spiky_grad(d, std::sqrt(r2));
                        weights[k] = w;
                        grads[k] = grad;
                        rho += w;
                        grad_i += grad;
                        sum_sq += glm::dot(grad, grad);
                    }
                    // Both sums in units of rest density: grad C = grad W / rho_0.
                    sum_sq = (sum_sq + glm::dot(grad_i, grad_i)) * (p.inv_rest * p.inv_rest);
                    const float ratio = rho * p.inv_rest;
                    density[i] = ratio;
                    const float c = ratio - 1.0f;
                    s.lambda[i] = c > 0.0f ? -c / (sum_sq + p.relaxation) : 0.0f;
                }
            });
            // Delta: gathered from the neighbours' lambdas, with the s_corr tensile term.
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const float li = s.lambda[i];
                    const std::size_t base = i * kMaxNeighbors;
                    const std::uint32_t* nb = &s.neighbors[base];
                    const glm::vec2* grads = &s.gradient[base];
                    const float* weights = &s.weight[base];
                    glm::vec2 sum{0.0f};
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const float w = weights[k] * p.inv_w_dq;
                        const float w2 = w * w;
                        const float scorr = -p.tensile * w2 * w2;
                        sum += (li + s.lambda[nb[k]] + scorr) * grads[k];
                    }
                    glm::vec2 delta = sum * (p.inv_rest * p.h);
                    const float len2 = glm::dot(delta, delta);
                    if (len2 > p.max_delta * p.max_delta) {
                        delta *= p.max_delta / std::sqrt(len2);
                    }
                    s.delta[i] = delta;
                }
            });
            for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    s.predicted[i] += s.delta[i];
                }
                project_range(p, begin, end, s);
            });
        }

        // Velocity from the solved positions, boundary response, then XSPH viscosity (gathered from the
        // neighbours' velocities into a second buffer).
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                glm::vec2 vel = (s.predicted[i] - x[i]) * inv_dt;
                const glm::vec2 nrm = s.normal[i];
                if (nrm.x != 0.0f || nrm.y != 0.0f) {
                    float vn = glm::dot(vel, nrm);
                    const float approach = glm::dot(s.pre_velocity[i], nrm);
                    if (approach < 0.0f) {
                        vn = std::max(vn, -p.restitution * approach);
                    }
                    const glm::vec2 vt = vel - glm::dot(vel, nrm) * nrm;
                    vel = vn * nrm + vt * (1.0f - p.friction);
                }
                s.velocity[i] = vel;
            }
        });
        for_ranges(jobs, n, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                glm::vec2 vel = s.velocity[i];
                if (p.viscosity > 0.0f) {
                    const glm::vec2 pi = s.predicted[i];
                    const std::uint32_t* nb = &s.neighbors[i * kMaxNeighbors];
                    glm::vec2 blend{0.0f};
                    for (std::uint32_t k = 0; k < s.neighbor_count[i]; ++k) {
                        const std::uint32_t j = nb[k];
                        const glm::vec2 d = (pi - s.predicted[j]) * p.inv_h;
                        blend += (s.velocity[j] - s.velocity[i]) * poly6(glm::dot(d, d));
                    }
                    vel += blend * (p.viscosity * p.inv_rest);
                }
                v[i] = vel;
                x[i] = s.predicted[i];
            }
        });
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
}

void run_fluid_2d(ecs::World& world, JobSystem* jobs) {
    auto view = world.view<Fluid2D>();
    bool any = false;
    for (ecs::Entity entity : view) {
        if (view.get<Fluid2D>(entity).count() > 0) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }
    const float dt = world.ctx<Time>().fixed_delta_time;
    const glm::vec3 gravity = world.ctx<Physics2DConfig>().gravity;
    // run_physics_2d ends with update_world_transforms, so the collider matrices are this tick's.
    const Boundaries boundaries = gather_boundaries(world);
    Fluid2DScratch& scratch = world.ctx<Fluid2DScratch>();
    for (ecs::Entity entity : view) {
        Fluid2DSolver::step(view.get<Fluid2D>(entity), boundaries, glm::vec2{gravity.x, gravity.y}, dt, jobs, scratch);
    }
}

}
