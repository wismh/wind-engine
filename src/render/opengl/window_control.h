#pragma once

#include "window_manager.h"
#include "window_system.h"

#include <engine/core/window_control.h>

namespace engine {

// Thin adapter so a game can request IWindowControl through DI (§4.2) instead of reaching into
// EngineRuntime directly. Holds a reference, not ownership — WindowManager outlives this for the
// lifetime of EngineRuntime (see EngineRuntime::Impl). open_window/close_window are the first
// WindowId-addressed methods here (§21.7); every other method still means kPrimaryWindow only,
// forwarded through windows_->primary_window().
class WindowControlImpl final : public IWindowControl {
public:
    explicit WindowControlImpl(WindowManager& windows) : windows_(&windows) {}

    void set_borderless(bool borderless, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_bordered(!borderless);
        }
    }

    void set_always_on_top(bool always_on_top, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_always_on_top(always_on_top);
        }
    }

    void set_position(glm::ivec2 position, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_position(position);
        }
    }

    void resize(glm::ivec2 size, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->resize(size);
        }
    }

    void set_click_through_enabled(bool enabled) override {
        windows_->primary_window().set_click_through_enabled(enabled);
    }

    void set_drag_region(std::optional<render::Rect> region, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_drag_region(region);
        }
    }

    std::optional<WindowId> open_window(const WindowDesc& desc) override {
        return windows_->create_window(desc);
    }

    void close_window(WindowId id) override {
        windows_->destroy_window(id);
    }

    [[nodiscard]] render::Rect usable_display_bounds(int display_index) const override {
        SDL_DisplayID id = SDL_GetPrimaryDisplay();
        int count = 0;
        if (SDL_DisplayID* displays = SDL_GetDisplays(&count)) {
            if (display_index >= 0 && display_index < count) {
                id = displays[display_index];
            }
            SDL_free(displays);
        }
        SDL_Rect bounds{};
        if (id == 0 || !SDL_GetDisplayUsableBounds(id, &bounds)) {
            return {};
        }
        return render::Rect{
                static_cast<float>(bounds.x),
                static_cast<float>(bounds.y),
                static_cast<float>(bounds.w),
                static_cast<float>(bounds.h),
        };
    }

private:
    WindowManager* windows_;
};

}
