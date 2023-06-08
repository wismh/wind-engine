#pragma once

#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <optional>

namespace engine::ui {

enum class UiFit {
    FillWindow,
    Fixed,
};

struct UiCanvas {
    AssetId document;
    std::optional<AssetId> stylesheet;
    std::shared_ptr<ViewModel> data_context;
    render::Rect rect{};
    UiFit fit = UiFit::FillWindow;
    int order = 0;
};

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
void apply_fill_window(ecs::World& world);
void handle_pointer(ecs::World& world, float x, float y);

}
