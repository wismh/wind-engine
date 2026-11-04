#pragma once

// docs/tech/modules/ECS.md (Grid queries)

#include <engine/ecs/ray.h>
#include <engine/render/aabb.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>

namespace engine {

// Grid queries work in grid space: cell (i, j, k) covers [i, i + 1) x [j, j + 1) x [k, k + 1). The cell size is 1 and
// the origin is 0; the caller scales world coordinates into that space (a ray's `t` survives the scaling when the
// direction is scaled too). +Y is up, so a box blocked while moving -Y is grounded. Coordinates are floats: keep them
// within about +-8192 cells for the skin below to stay above float precision. A coordinate or delta of 2^24 or more,
// where a float no longer tells one cell from the next, counts as non-finite.

// How far `sweep_aabb` lets a box sink into a face, or overlap a wall beside it, and still treat it as touching, in
// grid units (1/1000 of a cell). It absorbs the rounding of a world-to-grid conversion.
inline constexpr float kGridSkin = 1e-3f;

// A non-owning reference to a callable `bool(Cell)` that says whether a cell is solid. Like `std::function_ref`: two
// pointers, no allocation, one indirect call per cell. It refers to the callable, so it is valid only for the call it
// is passed to; never store one. A lambda written at the call site is the usual argument; pass a free function through
// a lambda or a function-pointer variable.
template <class Cell>
class CellPredicate {
public:
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, CellPredicate> &&
                 std::is_object_v<std::remove_reference_t<F>> && std::is_invocable_r_v<bool, F&, Cell>)
    CellPredicate(F&& f) noexcept // NOLINT(google-explicit-constructor): implicit like std::function_ref
        : object_(static_cast<const void*>(std::addressof(f))), call_(&invoke<std::remove_reference_t<F>>) {}

    bool operator()(Cell cell) const {
        return call_(object_, cell);
    }

private:
    template <class F>
    static bool invoke(const void* object, Cell cell) {
        return static_cast<bool>((*static_cast<F*>(const_cast<void*>(object)))(cell));
    }

    const void* object_;
    bool (*call_)(const void*, Cell);
};

// The first solid cell a ray reaches.
struct GridHit {
    glm::ivec3 cell{0};
    // The face the ray entered through: one axis, +-1, facing back along the ray, so `cell + normal` is the cell the
    // ray came from (where a block is placed). Zero when the ray starts inside a solid cell.
    glm::ivec3 normal{0};
    // `point = origin + t * direction`; a distance when the direction is unit length.
    float t = 0.0f;
    glm::vec3 point{0.0f};
};

struct GridHit2D {
    glm::ivec2 cell{0};
    glm::ivec2 normal{0};
    float t = 0.0f;
    glm::vec2 point{0.0f};
};

// Where a box ended up after `sweep_aabb`.
struct GridSweep {
    render::Aabb box;
    // The displacement actually applied: `box.min` minus the input `min`.
    glm::vec3 moved{0.0f};
    // Per axis: 0 free, +1 blocked while moving +axis, -1 blocked while moving -axis.
    glm::ivec3 hit{0};

    [[nodiscard]] bool grounded() const {
        return hit.y < 0;
    }
};

struct GridSweep2D {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    glm::vec2 moved{0.0f};
    glm::ivec2 hit{0};

    [[nodiscard]] bool grounded() const {
        return hit.y < 0;
    }
};

// Amanatides-Woo walk through the cells the ray crosses, up to `t <= max_distance` (`t` and `max_distance` are
// multiples of `ray.direction`, as in `ray.h`). A boundary is half-open in the direction of travel: a ray that starts
// on a face and points away does not touch the cell behind it. A ray that starts inside a solid cell reports `t = 0`
// and a zero normal, even with a zero direction or `max_distance = 0`. A ray that passes exactly through an edge or a
// corner is blocked by any solid cell touching it there (tested x, y, z, then xy, xz, yz, then the diagonal cell).
// nullopt for a miss, a zero direction outside a solid cell, `max_distance < 0`, or a non-finite input.
[[nodiscard]] std::optional<GridHit> raycast_grid(
        const Ray& ray, float max_distance, CellPredicate<glm::ivec3> is_solid);
[[nodiscard]] std::optional<GridHit2D> raycast_grid(
        glm::vec2 origin, glm::vec2 direction, float max_distance, CellPredicate<glm::ivec2> is_solid);

// Moves `box` by `delta` one axis at a time, Y then X then Z, stopping each axis flush against the first solid face
// ahead of the box. A long move is cut into `ceil(max |delta|)` sub-steps so a fast diagonal move cuts no corner larger
// than a cell; an axis blocked in one sub-step stays blocked, and the other axes keep sliding. Every face
// crossed along an axis is tested, so no speed tunnels through a wall. A box resting on a floor (or sunk up to
// `kGridSkin` into it) under a downward delta snaps to the face and reports `hit.y = -1` every call, without drift. A
// box flush against a wall or floor slides along it. A move that ends exactly on a face counts as blocked; a zero delta
// on an axis reports no hit there. Cells the box already overlaps deeper than the skin are ignored, so a box inside a
// solid is never stuck (and not pushed out). A reversed bound is swapped. A non-finite box or delta returns the box
// unmoved.
[[nodiscard]] GridSweep sweep_aabb(const render::Aabb& box, glm::vec3 delta, CellPredicate<glm::ivec3> is_solid);
[[nodiscard]] GridSweep2D sweep_aabb(
        glm::vec2 min, glm::vec2 max, glm::vec2 delta, CellPredicate<glm::ivec2> is_solid);

}
