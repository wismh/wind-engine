#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::ui {

enum class UiFit {
    FillWindow,
    Fixed,
    ScaleWithScreenSize,
};

struct UiCanvas {
    AssetId document;
    std::optional<AssetId> stylesheet;
    std::vector<AssetId> extra_stylesheets;
    std::shared_ptr<ViewModel> data_context;
    render::Rect rect{};
    glm::vec2 reference_size{0.0f, 0.0f};  // design resolution; required when fit == ScaleWithScreenSize
    UiFit fit = UiFit::FillWindow;
    int order = 0;
    WindowId window = kPrimaryWindow;   // which window's size drives this canvas's rect (SDD §21.6)
};

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

struct MouseConsumed {
    bool value = false;
};

struct WindowSize {
    int width = 0;
    int height = 0;
};

// Only ever holds entries for windows OTHER than kPrimaryWindow — the primary's size stays
// authoritative in the existing ctx<WindowSize>() singleton, unchanged, so every pre-existing
// single-window call site (including every ctx<WindowSize>() write across tests/) keeps working
// with zero modification. A canvas/window not present here has never been sized (not yet resized
// since creation) and resolves to {0, 0} via window_size_for() below.
struct WindowSizes {
    std::unordered_map<WindowId, WindowSize> sizes;
};

// Centralizes the "primary reads ctx<WindowSize>(), everything else reads ctx<WindowSizes>()"
// branch so callers (apply_canvas_fit, run_ui_render) don't duplicate it.
[[nodiscard]] WindowSize window_size_for(ecs::World& world, WindowId id);

struct WindowResizeEvent {
    WindowId window = kPrimaryWindow;
    int width = 0;
    int height = 0;
};

// Purely informational (SDD §21.7): the engine never quits or destroys a window on its own when
// the OS reports a close request — a game system reads this in its own schedule and decides
// (quit, confirm dialog, ignore, or call IWindowControl::close_window for a secondary window).
struct WindowCloseRequestedEvent {
    WindowId window = kPrimaryWindow;
};

struct UiPointer {
    glm::vec2 position{};
    bool down = false;
};

[[nodiscard]] constexpr bool rect_contains(const render::Rect& rect, float x, float y) noexcept {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

void begin_frame(ecs::World& world);
void apply_canvas_fit(ecs::World& world);
// `window` (default kPrimaryWindow, trailing so every pre-existing call site keeps compiling
// unchanged) restricts hit-testing to canvases whose UiCanvas::window matches — a canvas assigned
// to a different window never receives this pointer event's click (SDD §21.6).
void handle_pointer(ecs::World& world, float x, float y, WindowId window = kPrimaryWindow);

}
