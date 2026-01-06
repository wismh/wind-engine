#pragma once

#include "desktop_overlay_policy.h"
#include "window_manager.h"
#include "window_system.h"

#include <engine/core/window_control.h>

#include <functional>

namespace engine {

// Thin adapter so a game asks for IWindowControl through EngineServices. Holds references, not
// ownership — WindowManager and DesktopOverlayPolicy outlive this for the presentation's lifetime.
// on_windows_changed_ re-arms the modal-loop hook after open/close; that used to live only on
// EngineRuntime::open_window, which games never call.
class WindowControlImpl final : public IWindowControl {
public:
    WindowControlImpl(WindowManager& windows, DesktopOverlayPolicy& overlay) : windows_(&windows), overlay_(&overlay) {}

    void set_on_windows_changed(std::function<void()> callback) {
        on_windows_changed_ = std::move(callback);
    }

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

    [[nodiscard]] std::optional<glm::ivec2> position(WindowId window) const override {
        const WindowManager& windows = *windows_;
        if (const WindowSystem* target = windows.window(window)) {
            return target->position();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<glm::ivec2> size(WindowId window) const override {
        const WindowManager& windows = *windows_;
        if (const WindowSystem* target = windows.window(window)) {
            return target->size();
        }
        return std::nullopt;
    }

    void set_click_through_enabled(bool enabled, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_click_through_enabled(enabled);
        }
    }

    void set_overlay_mode(OverlayMode mode) override {
        overlay_->set_mode(mode);
    }

    [[nodiscard]] OverlayMode overlay_mode() const override {
        return overlay_->mode();
    }

    void set_drag_region(std::optional<render::Rect> region, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_drag_region(region);
        }
    }

    std::optional<WindowId> open_window(const WindowDesc& desc) override {
        const auto id = windows_->create_window(desc);
        if (on_windows_changed_) {
            on_windows_changed_();
        }
        return id;
    }

    void close_window(WindowId id) override {
        windows_->destroy_window(id);
        if (on_windows_changed_) {
            on_windows_changed_();
        }
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

    [[nodiscard]] render::Rect usable_display_bounds_for_window(WindowId window) const override {
        const WindowManager& windows = *windows_;
        if (const WindowSystem* target = windows.window(window)) {
            if (SDL_Window* sdl_win = target->window()) {
                SDL_DisplayID id = SDL_GetDisplayForWindow(sdl_win);
                if (id == 0) {
                    int wx = 0;
                    int wy = 0;
                    if (SDL_GetWindowPosition(sdl_win, &wx, &wy)) {
                        const glm::ivec2 sz = target->size();
                        const SDL_Point pt{wx + sz.x / 2, wy + sz.y / 2};
                        id = SDL_GetDisplayForPoint(&pt);
                    }
                }
                SDL_Rect bounds{};
                if (id != 0 && SDL_GetDisplayUsableBounds(id, &bounds)) {
                    return render::Rect{
                            static_cast<float>(bounds.x),
                            static_cast<float>(bounds.y),
                            static_cast<float>(bounds.w),
                            static_cast<float>(bounds.h),
                    };
                }
            }
        }
        return usable_display_bounds(0);
    }

private:
    WindowManager* windows_;
    DesktopOverlayPolicy* overlay_;
    std::function<void()> on_windows_changed_;
};

}
