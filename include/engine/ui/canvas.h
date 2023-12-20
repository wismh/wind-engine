#pragma once

#include <engine/core/key_code.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::ui {

enum class UiFit {
    FillWindow,
    Fixed,
    ScaleWithScreenSize,
};

struct UiCanvas {
    // If set, Bind clones `UiInstance` from AssetsDb when this id changes. If unset, the live
    // tree is `UiInstance` (C++ builder, splash); Bind only reapplies bindings. Style and input
    // do not care which way the tree was created.
    std::optional<AssetId> document;
    std::optional<AssetId> stylesheet;
    std::vector<AssetId> extra_stylesheets;
    std::shared_ptr<ViewModel> data_context;
    render::Rect rect{};
    glm::vec2 reference_size{0.0f, 0.0f};  // design resolution; required when fit == ScaleWithScreenSize
    UiFit fit = UiFit::FillWindow;
    int order = 0;
    WindowId window = kPrimaryWindow;   // which window's size drives this canvas's rect
};

// Spawns a canvas whose live tree is `document` (optional in-memory stylesheet). Clears
// `canvas.document` so Bind will not replace the instance from AssetsDb.
[[nodiscard]] ecs::Entity spawn_canvas(ecs::World& world, UiCanvas canvas, UiDocument document,
        std::optional<Stylesheet> stylesheet = {});

// Maps a canvas's `rect` + `fit` to the coordinate space layout/hit-test should run in:
// FillWindow/Fixed lay out directly in `rect` (real pixels, offset {0,0}, scale 1).
// ScaleWithScreenSize lays out in fixed `reference_size` design units; `offset`/`scale`
// convert that design space back to the real-pixel `rect` engine wrote via apply_canvas_fit.
struct UiCanvasSpace {
    render::Rect layout_rect{};
    glm::vec2 offset{0.0f, 0.0f};
    float scale = 1.0f;
    bool reference_space = false;
};

[[nodiscard]] UiCanvasSpace canvas_layout_space(const render::Rect& rect, UiFit fit, glm::vec2 reference_size);

// One WindowId-keyed set, kPrimaryWindow included like any other window (wind-107) — there is no
// separate "primary" flag; consumed_for(kPrimaryWindow) is just a lookup like any other id.
struct MouseConsumed {
    std::unordered_set<WindowId> consumed_windows;

    [[nodiscard]] bool consumed_for(WindowId window = kPrimaryWindow) const noexcept {
        return consumed_windows.contains(window);
    }
};

struct WindowSize {
    int width = 0;
    int height = 0;
};

// Every live window's last-known drawable size, keyed by WindowId — kPrimaryWindow included like
// any other window (wind-107). A WindowId with no entry yet (never resized/backfilled since the
// window was created) resolves to {0, 0} via window_size_for() below.
struct WindowSizes {
    std::unordered_map<WindowId, WindowSize> sizes;
};

[[nodiscard]] WindowSize window_size_for(ecs::World& world, WindowId id);

struct WindowResizeEvent {
    WindowId window = kPrimaryWindow;
    int width = 0;
    int height = 0;
};

// Purely informational: the engine never quits or destroys a window on its own when
// the OS reports a close request — a game system reads this in its own schedule and decides
// (quit, confirm dialog, ignore, or call IWindowControl::close_window for a secondary window).
struct WindowCloseRequestedEvent {
    WindowId window = kPrimaryWindow;
};

struct UiPointer {
    glm::vec2 position{};
    bool down = false;
};

// State of one in-progress `drag="{binding}"` (canvas.h/document.h Element::drag_binding),
// captured at pointer-down so update_drag() can keep recomputing the fraction from the same
// geometry on every subsequent Move — including once the pointer leaves `rect` — without
// re-hit-testing (a fresh hit-test would lose the drag the moment the cursor left the element).
struct ActiveDrag {
    ecs::Entity canvas_entity{};
    BindingId value_binding{};
    render::Rect rect{};
    glm::vec2 space_offset{0.0f, 0.0f};
    float space_scale = 1.0f;
    StackDirection orientation = StackDirection::Horizontal;
    // Element::generated_owner of the drag-start element, when it was generated inside an
    // ItemsControl/ItemTemplate — nullptr for an ordinary (non-templated) drag target. Never
    // dereferenced directly from here across frames; update_drag() re-validates it against a
    // freshly re-bound tree (document.cpp find_by_generated_owner()) before writing through it,
    // since the game is free to remove that item from its list between Move events mid-drag.
    const void* owner = nullptr;
};

// One map for every window (kPrimaryWindow included like any other key) — unlike UiPointer/
// UiPointers above, this is new state with no pre-existing single-window call site to keep
// source-compatible, so it doesn't need that split.
struct UiActiveDrags {
    std::unordered_map<WindowId, ActiveDrag> drags;
};

// In-progress Viewport pan (empty-background drag). Separate from ActiveDrag: this writes 2D pan
// from pointer delta, not a [0,1] fraction along one axis.
struct ActivePan {
    ecs::Entity canvas_entity{};
    BindingId pan_x_binding{};
    BindingId pan_y_binding{};
    BindingId zoom_binding{};
    glm::vec2 last_pointer{};
    glm::vec2 space_offset{0.0f, 0.0f};
    float space_scale = 1.0f;
    const void* owner = nullptr;
};

struct UiActivePans {
    std::unordered_map<WindowId, ActivePan> pans;
};

struct ActiveScrollbarDrag {
    ecs::Entity canvas_entity{};
    std::vector<std::size_t> path;
    const void* owner = nullptr;
    float drag_start_pointer_y = 0.0f;
    float drag_start_scroll_y = 0.0f;
    float track_h = 0.0f;
    float thumb_h = 0.0f;
    float max_scroll_y = 0.0f;
    glm::vec2 space_offset{0.0f, 0.0f};
    float space_scale = 1.0f;
    BindingId scroll_y_binding{};
};

struct UiActiveScrollbars {
    std::unordered_map<WindowId, ActiveScrollbarDrag> drags;
};

// Only ever holds entries for windows OTHER than kPrimaryWindow — mirrors WindowSizes above:
// the primary's pointer stays authoritative in the existing ctx<UiPointer>() singleton, unchanged,
// so every pre-existing single-window call site keeps working with zero modification. A window
// whose pointer has never moved resolves to a default-constructed UiPointer via pointer_for().
struct UiPointers {
    std::unordered_map<WindowId, UiPointer> pointers;
};

// Centralizes the "primary reads ctx<UiPointer>(), everything else reads ctx<UiPointers>()" branch
// (same shape as window_size_for) so a pointer move/click in one window never leaks its position or
// down-state into another window's hover/press paint state.
[[nodiscard]] UiPointer& pointer_for(ecs::World& world, WindowId id);

[[nodiscard]] constexpr bool rect_contains(const render::Rect& rect, float x, float y) noexcept {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

void begin_frame(ecs::World& world);
void apply_canvas_fit(ecs::World& world);
// `window` (default kPrimaryWindow, trailing so every pre-existing call site keeps compiling
// unchanged) restricts hit-testing to canvases whose UiCanvas::window matches — a canvas assigned
// to a different window never receives this pointer event's click.
void handle_pointer(ecs::World& world, float x, float y, WindowId window = kPrimaryWindow);
// Same hit test as handle_pointer() (and updates MouseConsumed the same way) but never executes a
// command — for MouseEvent::Kind::Move, where re-running a bound element's command on every hover
// pixel would be wrong. Without this, MouseConsumed only ever reflects the pointer's position at
// the last click, so click_through stays wrong for every frame the pointer merely
// moves over (or off of) a UI element without clicking.
void update_pointer_hover(ecs::World& world, float x, float y, WindowId window = kPrimaryWindow);

// Recomputes the in-progress drag (if any) for `window` from its captured start geometry — not a
// fresh hit-test, so the drag keeps tracking (x, y) even once the pointer has left the dragged
// element's bounds — and writes the new [0,1] fraction into the bound ViewModel property. A no-op
// if no drag is active for this window (e.g. the Move didn't follow a Down on a `drag`-bound
// element).
void update_drag(ecs::World& world, float x, float y, WindowId window = kPrimaryWindow);
void end_drag(ecs::World& world, WindowId window = kPrimaryWindow);

void update_pan(ecs::World& world, float x, float y, WindowId window = kPrimaryWindow);
void end_pan(ecs::World& world, WindowId window = kPrimaryWindow);

// Wheel zoom-to-cursor on the innermost Viewport under (x, y). No-op if zoom is unbound.
void handle_wheel(ecs::World& world, float x, float y, float wheel_y, WindowId window = kPrimaryWindow);

struct UiFocus {
    ecs::Entity canvas_entity{};
    Element* element = nullptr;
};

struct UiFocusState {
    std::unordered_map<WindowId, UiFocus> focused;
};

void set_focus(ecs::World& world, WindowId window, ecs::Entity canvas_entity, Element* element);
void clear_focus(ecs::World& world, WindowId window = kPrimaryWindow);
[[nodiscard]] Element* focused_element(ecs::World& world, WindowId window = kPrimaryWindow);

void handle_key(ecs::World& world, KeyCode key, bool down, bool repeat = false, WindowId window = kPrimaryWindow);
void handle_text_input(ecs::World& world, std::string_view text, WindowId window = kPrimaryWindow);

}
