#pragma once

#include "desktop_overlay_policy.h"
#include "window_manager.h"
#include "window_system.h"

#include "core/file_dialog_state.h"

#include <engine/core/window_control.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// Thin adapter so a game asks for IWindowControl through EngineServices. Holds references, not
// ownership — WindowManager and DesktopOverlayPolicy outlive this for the presentation's lifetime.
// on_windows_changed_ re-arms the modal-loop hook after open/close; that used to live only on
// EngineRuntime::open_window, which games never call.
class WindowControlImpl final : public IWindowControl {
public:
    WindowControlImpl(WindowManager& windows, DesktopOverlayPolicy& overlay,
            std::shared_ptr<FileDialogCompletions> dialogs)
        : windows_(&windows)
        , overlay_(&overlay)
        , dialogs_(std::move(dialogs)) {}

    void set_on_windows_changed(std::function<void()> callback) {
        on_windows_changed_ = std::move(callback);
    }

    void set_title(std::string_view title, WindowId window) override {
        if (WindowSystem* target = windows_->window(window)) {
            target->set_title(title);
        }
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

    void set_vsync(bool enabled) override {
        windows_->set_vsync(enabled);
    }

    [[nodiscard]] bool vsync() const override {
        return windows_->vsync();
    }

    void set_max_fps(int fps) override {
        windows_->set_max_fps(fps);
    }

    [[nodiscard]] int max_fps() const override {
        return windows_->max_fps();
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

    [[nodiscard]] std::vector<WindowId> open_windows() const override {
        std::vector<WindowId> ids;
        const WindowManager& windows = *windows_;
        windows.for_each_window([&ids](WindowId id, const WindowSystem&) { ids.push_back(id); });
        return ids;
    }

    FileDialogCall request_open_file(WindowId owner, std::vector<FileFilter> filters) override {
        auto state = std::make_shared<FileDialogState>();
        // SDL reads the filter array until the callback runs, possibly on another thread, so the strings
        // and the array live in this heap block until then.
        auto call = std::make_unique<DialogCall>();
        call->dialogs = dialogs_;
        call->state = state;
        call->filters = std::move(filters);
        call->sdl_filters.reserve(call->filters.size());
        for (const FileFilter& filter : call->filters) {
            call->sdl_filters.push_back(SDL_DialogFileFilter{filter.name.c_str(), filter.pattern.c_str()});
        }
        SDL_Window* const parent = windows_->window(owner) != nullptr ? windows_->window(owner)->window() : nullptr;
        const int count = static_cast<int>(call->sdl_filters.size());
        const SDL_DialogFileFilter* const list = count > 0 ? call->sdl_filters.data() : nullptr;
        SDL_ShowOpenFileDialog(&on_open_file, call.release(), parent, list, count, nullptr, false);
        return FileDialogCall{std::move(state)};
    }

    FileDialogCall request_open_folder(WindowId owner, std::filesystem::path start) override {
        auto state = std::make_shared<FileDialogState>();
        // SDL copies default_location before it returns; the call block only carries the answer's destination.
        auto call = std::make_unique<DialogCall>();
        call->dialogs = dialogs_;
        call->state = state;
        std::error_code error;
        const std::u8string location =
                !start.empty() && std::filesystem::is_directory(start, error) ? start.u8string() : std::u8string{};
        SDL_Window* const parent = windows_->window(owner) != nullptr ? windows_->window(owner)->window() : nullptr;
        SDL_ShowOpenFolderDialog(&on_open_file, call.release(), parent,
                location.empty() ? nullptr : reinterpret_cast<const char*>(location.c_str()), false);
        return FileDialogCall{std::move(state)};
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
    struct DialogCall {
        std::shared_ptr<FileDialogCompletions> dialogs;
        std::shared_ptr<FileDialogState> state;
        std::vector<FileFilter> filters;
        std::vector<SDL_DialogFileFilter> sdl_filters;
    };

    // SDL may call this on its dialog thread, so it only pushes into the queue. The queue and the state are
    // shared, so they outlive a presentation that shut down, or a call dropped, while the dialog was open.
    static void SDLCALL on_open_file(void* userdata, const char* const* files, int) {
        const std::unique_ptr<DialogCall> call(static_cast<DialogCall*>(userdata));
        std::optional<std::filesystem::path> path;
        if (files != nullptr && files[0] != nullptr) {
            path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
        }
        call->dialogs->push(std::move(call->state), FileDialogResult{.path = std::move(path)});
    }

    WindowManager* windows_;
    DesktopOverlayPolicy* overlay_;
    std::shared_ptr<FileDialogCompletions> dialogs_;
    std::function<void()> on_windows_changed_;
};

}
