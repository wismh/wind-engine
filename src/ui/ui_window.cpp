#include <engine/ui/ui_window.h>

#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ui/document.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {

// The part of a UiWindow the world's system also reads. Heap-allocated so a moved UiWindow keeps its address.
struct UiWindowState {
    IWindowControl* windows = nullptr;
    Worlds* worlds = nullptr;
    ecs::World* world = nullptr;
    WindowId id{};
    ecs::Entity canvas{};
    bool close_requested = false;
    bool closed = false;
    std::optional<render::Rect> drag_region;
    std::vector<render::Rect> exclusions;
};

namespace {

// The live UiWindows of one world, and the close events already seen. Lives in the world's context.
struct UiWindowRegistry {
    std::vector<UiWindowState*> windows;
    ecs::EventCursor<ui::WindowCloseRequestedEvent> closes;
};

void remove_canvases(ecs::World& world, WindowId id) {
    std::vector<ecs::Entity> doomed;
    auto canvases = world.view<ui::UiCanvas>();
    for (const ecs::Entity entity : canvases) {
        if (canvases.get<ui::UiCanvas>(entity).window == id) {
            doomed.push_back(entity);
        }
    }
    for (const ecs::Entity entity : doomed) {
        world.destroy(entity);
    }
}

// The interactive rectangles of every canvas on `id`, in window client pixels.
std::vector<render::Rect> interactive_rects(ecs::World& world, WindowId id) {
    std::vector<render::Rect> rects;
    auto canvases = world.view<ui::UiCanvas>();
    for (const ecs::Entity entity : canvases) {
        const ui::UiCanvas& canvas = canvases.get<ui::UiCanvas>(entity);
        const ui::UiInstance* instance = world.try_get<ui::UiInstance>(entity);
        if (canvas.window != id || instance == nullptr) {
            continue;
        }
        const ui::UiCanvasSpace space = ui::canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
        std::vector<render::Rect> in_layout;
        ui::collect_interactive_rects(instance->document.root, in_layout);
        for (const render::Rect& rect : in_layout) {
            rects.push_back(render::Rect{space.offset.x + rect.x * space.scale, space.offset.y + rect.y * space.scale,
                    rect.w * space.scale, rect.h * space.scale});
        }
    }
    return rects;
}

bool same_rects(const std::vector<render::Rect>& a, const std::vector<render::Rect>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h) {
            return false;
        }
    }
    return true;
}

}

glm::ivec2 centered_position(render::Rect area, glm::ivec2 size) noexcept {
    return {static_cast<int>(std::floor(area.x + (area.w - static_cast<float>(size.x)) * 0.5f)),
            static_cast<int>(std::floor(area.y + (area.h - static_cast<float>(size.y)) * 0.5f))};
}

// Frame / Input, registered with the UI systems. Routes each close request to the UiWindow of that window, and keeps
// the drag exclusions of the windows that have a drag region in step with the interactive elements the last layout
// found.
void run_ui_windows(ecs::World& world) {
    UiWindowRegistry& registry = world.ctx<UiWindowRegistry>();
    for (const ui::WindowCloseRequestedEvent& event :
            ecs::EventReader<ui::WindowCloseRequestedEvent>{world, registry.closes}) {
        for (UiWindowState* state : registry.windows) {
            if (state->id == event.window) {
                state->close_requested = true;
            }
        }
    }
    for (UiWindowState* state : registry.windows) {
        if (!state->drag_region || state->closed) {
            continue;
        }
        std::vector<render::Rect> holes = interactive_rects(world, state->id);
        if (!same_rects(holes, state->exclusions)) {
            state->exclusions = holes;
            state->windows->set_drag_exclusions(std::move(holes), state->id);
        }
    }
}

std::expected<UiWindow, UiWindowError> UiWindow::open(IWindowControl& windows, Worlds& worlds, ecs::World& world,
        const WindowDesc& desc, ui::UiCanvas canvas) {
    const std::optional<WindowId> id = windows.open_window(desc);
    if (!id) {
        return std::unexpected(UiWindowError::WindowRefused);
    }
    worlds.bind_window(*id, world);
    worlds.enable_ui(world);
    canvas.window = *id;
    const ecs::Entity entity = world.create();
    world.emplace<ui::UiCanvas>(entity, std::move(canvas));

    auto state = std::make_unique<UiWindowState>();
    state->windows = &windows;
    state->worlds = &worlds;
    state->world = &world;
    state->id = *id;
    state->canvas = entity;
    world.ctx<UiWindowRegistry>().windows.push_back(state.get());
    return UiWindow(std::move(state));
}

UiWindow::UiWindow(std::unique_ptr<UiWindowState> state) noexcept : state_(std::move(state)) {}

UiWindow::UiWindow(UiWindow&& other) noexcept : state_(std::move(other.state_)) {}

UiWindow& UiWindow::operator=(UiWindow&& other) noexcept {
    if (this != &other) {
        close();
        state_ = std::move(other.state_);
    }
    return *this;
}

UiWindow::~UiWindow() {
    close();
}

WindowId UiWindow::id() const noexcept {
    return state_ ? state_->id : WindowId{};
}

ecs::Entity UiWindow::canvas() const noexcept {
    return state_ ? state_->canvas : ecs::Entity{};
}

bool UiWindow::is_open() const {
    if (!state_ || state_->closed) {
        return false;
    }
    const std::vector<WindowId> open = state_->windows->open_windows();
    return std::ranges::find(open, state_->id) != open.end();
}

bool UiWindow::take_close_requested() {
    if (!state_ || !state_->close_requested) {
        return false;
    }
    state_->close_requested = false;
    return true;
}

void UiWindow::close() {
    if (!state_ || state_->closed) {
        return;
    }
    UiWindowState& state = *state_;
    state.closed = true;
    // The world may be gone (Worlds::destroy unbinds its windows first): then there is nothing of it to touch.
    if (state.worlds->world_for(state.id) == state.world) {
        UiWindowRegistry& registry = state.world->ctx<UiWindowRegistry>();
        std::erase(registry.windows, state_.get());
        remove_canvases(*state.world, state.id);
        state.worlds->unbind_window(state.id);
    }
    // The platform closes an owned window with its owner; closing it again would be a second close of nothing.
    const std::vector<WindowId> open = state.windows->open_windows();
    if (std::ranges::find(open, state.id) != open.end()) {
        state.windows->close_window(state.id);
    }
}

void UiWindow::set_drag_region(std::optional<render::Rect> region) {
    if (!state_ || state_->closed) {
        return;
    }
    state_->drag_region = region;
    state_->windows->set_drag_region(region, state_->id);
    if (!region) {
        state_->exclusions.clear();
        state_->windows->set_drag_exclusions({}, state_->id);
    }
}

void UiWindow::center_on_work_area() {
    if (!state_ || state_->closed) {
        return;
    }
    const render::Rect area = state_->windows->usable_display_bounds_for_window(state_->id);
    const std::optional<glm::ivec2> size = state_->windows->size(state_->id);
    if (!size || area.w <= 0.0f || area.h <= 0.0f) {
        return;
    }
    state_->windows->set_position(centered_position(area, *size), state_->id);
}

}
