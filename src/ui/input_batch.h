#pragma once

// wind ui_scrollview_perf_plan.md — Крок 4: internal-only per-run_input() dedup cache.
//
// Within a single run_input() call (ecs/systems.cpp, Phase::Input), several MouseEvents queued
// for one frame (typically a few Move events, or Wheel events during an active scroll) each
// independently resolve the topmost UiCanvas under the pointer and re-run apply_bindings() +
// apply_layout_style() + layout() for it (canvas.cpp prepare_top_canvas(), plus update_drag()'s
// and update_pan()'s own separate apply_bindings() calls for an item-owned drag/pan). Nothing
// changes ViewModel data between those events inside one run_input() call — no Game-phase system
// runs in between — so only the first touch of a given canvas entity in the batch needs the full
// bind+layout; later touches in the same batch can reuse it.
//
// Deliberately NOT ctx<>() state. A ctx<>()-resident monotonic batch counter would persist across
// *every* call, including the many existing tests that call handle_pointer()/
// update_pointer_hover()/handle_wheel()/update_drag()/update_pan() directly, never through
// run_input() — such a counter would never advance for them, so a second direct call in a test
// would wrongly look like "still the same batch" and reuse stale bind/layout state even though
// the test explicitly changed the ViewModel or scroll position between two calls. Instead, a
// UiInputBatchCache exists only on run_input()'s own stack, for the lifetime of one run_input()
// call, and is passed down explicitly as `UiInputBatchCache&` to the *_for_run_input() entry
// points below — a separate, parallel set of functions from (and never called by) the public
// handle_pointer()/update_pointer_hover()/handle_wheel()/update_drag()/update_pan() declared in
// <engine/ui/canvas.h>. Every call to those public functions — from a test, from any other
// engine code, anywhere outside run_input() — takes the normal no-batch (`batch == nullptr`)
// path unchanged: always a fresh, full bind+layout, exactly as before this optimization existed.

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <cstddef>
#include <vector>

namespace engine::ui {

struct UiInputBatchCache {
    // Canvas entities prepare_top_canvas() has already fully bound + styled + laid out during
    // this run_input() call. Small (at most one entry per on-screen canvas actually touched this
    // frame), so a linear scan beats hashing Entity (which has no std::hash specialization in
    // this engine — see include/engine/ecs/entity.h).
    std::vector<ecs::Entity> prepared;

    // How many times prepare_top_canvas() actually ran the full bind+style+layout path this
    // batch (i.e. how many distinct canvas entities got prepared) — exposed for tests; production
    // code never reads it.
    std::size_t full_recompute_count = 0;

    // How many times update_drag()/update_pan()'s own item-owned apply_bindings() call was
    // skipped because prepare_top_canvas() already covered that canvas entity this batch —
    // exposed for tests; production code never reads it.
    std::size_t drag_or_pan_bindings_reused_count = 0;

    [[nodiscard]] bool contains(ecs::Entity entity) const noexcept {
        for (const ecs::Entity e : prepared) {
            if (e == entity) {
                return true;
            }
        }
        return false;
    }

    // Only prepare_top_canvas() (canvas.cpp) calls this, after it finishes the full bind+style+
    // layout pass for `entity` — membership here is a promise that entity's UiInstance::document
    // has a fresh layout_rect this batch, which is what update_drag()/update_pan() below rely on
    // before skipping their own apply_bindings() call. They must never call mark() themselves:
    // their own apply_bindings()-only pass never runs layout(), so it cannot make that promise.
    void mark(ecs::Entity entity) {
        if (!contains(entity)) {
            prepared.push_back(entity);
            ++full_recompute_count;
        }
    }
};

// Batch-aware counterparts of the public functions of the same name in <engine/ui/canvas.h>.
// Only run_input() (src/ecs/systems.cpp) calls these; `batch` must outlive the call and should be
// a local variable scoped to exactly one run_input() invocation — never a ctx<>() resource.
void handle_pointer_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch);
void update_pointer_hover_for_run_input(
        ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch);
void handle_wheel_for_run_input(
        ecs::World& world, float x, float y, float wheel_y, WindowId window, UiInputBatchCache& batch);
void update_drag_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch);
void update_pan_for_run_input(ecs::World& world, float x, float y, WindowId window, UiInputBatchCache& batch);

}
