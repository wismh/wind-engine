#pragma once

#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>
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

struct WindowResizeEvent {
    int width = 0;
    int height = 0;
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
void handle_pointer(ecs::World& world, float x, float y);

}
