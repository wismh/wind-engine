#include <engine/ecs/grid_query.h>

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace engine {
namespace {

// Coordinates past this are rejected: `floor` to `int` must not overflow, and floats there have no fraction anyway.
constexpr float kMaxCoordinate = 1.0e9f;
// A direction component below this never steps on its axis.
constexpr float kZeroComponent = 1.0e-20f;
// Boundaries whose `t` lies within this (relative) of the nearest one are crossed together: an edge or a corner.
constexpr float kTieTolerance = 1.0e-6f;
constexpr int kMaxSweepSteps = 64;

template <int N>
using Vec = glm::vec<N, float>;
template <int N>
using IVec = glm::vec<N, int>;

template <int N>
struct RayResult {
    IVec<N> cell{0};
    IVec<N> normal{0};
    float t = 0.0f;
    Vec<N> point{0.0f};
};

template <int N>
bool finite(const Vec<N>& v) {
    for (int a = 0; a < N; ++a) {
        if (!std::isfinite(v[a]) || std::abs(v[a]) > kMaxCoordinate) {
            return false;
        }
    }
    return true;
}

int floor_int(float value) {
    return static_cast<int>(std::floor(value));
}

int ceil_int(float value) {
    return static_cast<int>(std::ceil(value));
}

template <int N>
std::optional<RayResult<N>> raycast(
        const Vec<N>& origin, const Vec<N>& direction, float max_distance, CellPredicate<IVec<N>> is_solid) {
    if (!finite<N>(origin) || !finite<N>(direction) || std::isnan(max_distance) || max_distance < 0.0f) {
        return std::nullopt;
    }

    IVec<N> cell{0};
    for (int a = 0; a < N; ++a) {
        cell[a] = floor_int(origin[a]);
        // Half-open in the direction of travel: on a boundary, a ray going down starts in the cell below it.
        if (direction[a] < 0.0f && static_cast<float>(cell[a]) == origin[a]) {
            --cell[a];
        }
    }
    if (is_solid(cell)) {
        return RayResult<N>{cell, IVec<N>{0}, 0.0f, origin};
    }

    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    IVec<N> step{0};
    Vec<N> t_max{kInfinity};
    Vec<N> t_delta{kInfinity};
    double budget = 3.0;
    bool moves = false;
    for (int a = 0; a < N; ++a) {
        const float d = direction[a];
        if (std::abs(d) < kZeroComponent) {
            continue;
        }
        moves = true;
        step[a] = d > 0.0f ? 1 : -1;
        const float boundary = static_cast<float>(d > 0.0f ? cell[a] + 1 : cell[a]);
        t_max[a] = (boundary - origin[a]) / d;
        t_delta[a] = 1.0f / std::abs(d);
        if (std::isfinite(max_distance)) {
            budget += std::ceil(static_cast<double>(max_distance) * std::abs(static_cast<double>(d))) + 1.0;
        } else {
            budget = static_cast<double>(1 << 24);
        }
    }
    if (!moves) {
        return std::nullopt;
    }

    const int iterations = static_cast<int>(std::min(budget, static_cast<double>(1 << 24)));
    for (int i = 0; i < iterations; ++i) {
        float t = kInfinity;
        for (int a = 0; a < N; ++a) {
            t = std::min(t, t_max[a]);
        }
        if (!std::isfinite(t) || t > max_distance) {
            return std::nullopt;
        }

        const float tolerance = kTieTolerance * std::max(1.0f, t);
        std::array<int, N> tied{};
        int tied_count = 0;
        for (int a = 0; a < N; ++a) {
            if (t_max[a] <= t + tolerance) {
                tied[static_cast<std::size_t>(tied_count++)] = a;
            }
        }

        const Vec<N> point = origin + t * direction;
        // Every non-empty subset of the tied axes, smallest first and in axis order within a size: the side cells of
        // an edge or corner, then the diagonal cell. With one tied axis that is just the next cell.
        const unsigned full = (1u << tied_count) - 1u;
        for (int size = 1; size <= tied_count; ++size) {
            for (unsigned mask = 1; mask <= full; ++mask) {
                if (std::popcount(mask) != size) {
                    continue;
                }
                IVec<N> candidate = cell;
                IVec<N> normal{0};
                bool first = true;
                for (int s = 0; s < tied_count; ++s) {
                    if ((mask & (1u << s)) == 0) {
                        continue;
                    }
                    const int a = tied[static_cast<std::size_t>(s)];
                    candidate[a] += step[a];
                    if (first) {
                        normal[a] = -step[a];
                        first = false;
                    }
                }
                if (is_solid(candidate)) {
                    return RayResult<N>{candidate, normal, t, point};
                }
            }
        }

        for (int s = 0; s < tied_count; ++s) {
            const int a = tied[static_cast<std::size_t>(s)];
            cell[a] += step[a];
            t_max[a] += t_delta[a];
        }
    }
    return std::nullopt;
}

template <int N>
struct SweepState {
    Vec<N> min{0.0f};
    Vec<N> max{0.0f};
    IVec<N> hit{0};
};

// Any solid cell in the box of cells lo..hi (inclusive on every axis).
template <int N>
bool any_solid(const IVec<N>& lo, const IVec<N>& hi, CellPredicate<IVec<N>> is_solid) {
    for (int a = 0; a < N; ++a) {
        if (hi[a] < lo[a]) {
            return false;
        }
    }
    IVec<N> cell = lo;
    while (true) {
        if (is_solid(cell)) {
            return true;
        }
        int a = 0;
        for (; a < N; ++a) {
            if (cell[a] < hi[a]) {
                ++cell[a];
                break;
            }
            cell[a] = lo[a];
        }
        if (a == N) {
            return false;
        }
    }
}

template <int N>
void sweep_axis(SweepState<N>& state, const Vec<N>& extent, int axis, float d, CellPredicate<IVec<N>> is_solid) {
    // The cells the box covers across the motion, shrunk by the skin so a face it rests on or slides along is not one.
    IVec<N> lo{0};
    IVec<N> hi{0};
    for (int b = 0; b < N; ++b) {
        if (b == axis) {
            continue;
        }
        if (extent[b] <= 2.0f * kGridSkin) {
            lo[b] = hi[b] = floor_int((state.min[b] + state.max[b]) * 0.5f);
        } else {
            lo[b] = floor_int(state.min[b] + kGridSkin);
            hi[b] = ceil_int(state.max[b] - kGridSkin) - 1;
        }
    }

    if (d < 0.0f) {
        const float leading = state.min[axis];
        const int first = floor_int(leading + kGridSkin);
        const int last = ceil_int(leading + d);
        for (int face = first; face >= last; --face) {
            lo[axis] = hi[axis] = face - 1;
            if (any_solid<N>(lo, hi, is_solid)) {
                state.min[axis] = static_cast<float>(face);
                state.max[axis] = static_cast<float>(face) + extent[axis];
                state.hit[axis] = -1;
                return;
            }
        }
    } else {
        const float leading = state.max[axis];
        const int first = ceil_int(leading - kGridSkin);
        const int last = floor_int(leading + d);
        for (int face = first; face <= last; ++face) {
            lo[axis] = hi[axis] = face;
            if (any_solid<N>(lo, hi, is_solid)) {
                state.max[axis] = static_cast<float>(face);
                state.min[axis] = static_cast<float>(face) - extent[axis];
                state.hit[axis] = 1;
                return;
            }
        }
    }
    state.min[axis] += d;
    state.max[axis] += d;
}

template <int N>
SweepState<N> sweep(Vec<N> min, Vec<N> max, const Vec<N>& delta, CellPredicate<IVec<N>> is_solid) {
    for (int a = 0; a < N; ++a) {
        if (min[a] > max[a]) {
            std::swap(min[a], max[a]);
        }
    }
    SweepState<N> state{min, max, IVec<N>{0}};
    if (!finite<N>(min) || !finite<N>(max) || !finite<N>(delta)) {
        return state;
    }

    float largest = 0.0f;
    for (int a = 0; a < N; ++a) {
        largest = std::max(largest, std::abs(delta[a]));
    }
    if (largest == 0.0f) {
        return state;
    }
    const int steps = std::clamp(static_cast<int>(std::ceil(largest)), 1, kMaxSweepSteps);
    const Vec<N> step = delta / static_cast<float>(steps);
    const Vec<N> extent = max - min;

    // Y first, so the ground is found before the box slides; then X, then Z.
    constexpr std::array<int, 3> kOrder{1, 0, 2};
    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < N; ++i) {
            const int axis = kOrder[static_cast<std::size_t>(i)];
            if (state.hit[axis] != 0 || step[axis] == 0.0f) {
                continue;
            }
            sweep_axis<N>(state, extent, axis, step[axis], is_solid);
        }
    }
    return state;
}

}

std::optional<GridHit> raycast_grid(const Ray& ray, float max_distance, CellPredicate<glm::ivec3> is_solid) {
    const auto hit = raycast<3>(ray.origin, ray.direction, max_distance, is_solid);
    if (!hit) {
        return std::nullopt;
    }
    return GridHit{hit->cell, hit->normal, hit->t, hit->point};
}

std::optional<GridHit2D> raycast_grid(
        glm::vec2 origin, glm::vec2 direction, float max_distance, CellPredicate<glm::ivec2> is_solid) {
    const auto hit = raycast<2>(origin, direction, max_distance, is_solid);
    if (!hit) {
        return std::nullopt;
    }
    return GridHit2D{hit->cell, hit->normal, hit->t, hit->point};
}

GridSweep sweep_aabb(const render::Aabb& box, glm::vec3 delta, CellPredicate<glm::ivec3> is_solid) {
    const glm::vec3 start = glm::min(box.min, box.max);
    const SweepState<3> state = sweep<3>(box.min, box.max, delta, is_solid);
    return GridSweep{render::Aabb{state.min, state.max}, state.min - start, state.hit};
}

GridSweep2D sweep_aabb(glm::vec2 min, glm::vec2 max, glm::vec2 delta, CellPredicate<glm::ivec2> is_solid) {
    const glm::vec2 start = glm::min(min, max);
    const SweepState<2> state = sweep<2>(min, max, delta, is_solid);
    return GridSweep2D{state.min, state.max, state.min - start, state.hit};
}

}
