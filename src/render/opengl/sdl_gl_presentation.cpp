#include "sdl_gl_presentation.h"

#include "clipboard.h"
#include "opengl_backend.h"
#include "opengl_canvas.h"
#include "opengl_factory.h"
#include "window_control.h"
#include "window_manager.h"

#include "core/back_key_filter.h"
#include "core/event_window.h"
#include "ui/painter.h"

#include <engine/builtin_ids.h>
#include <engine/core/app_lifecycle.h>
#include <engine/core/input_system.h>
#include <engine/core/key_code.h>
#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/font.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace engine {
namespace {

MouseButton mouse_button_from_sdl(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        default:
            return MouseButton::None;
    }
}

// Starts/stops each window's SDL text-input session for a focused enabled TextInput only.
// Android reads window->text_input_rect inside ShowScreenKeyboard, which runs from Start, and
// does not implement UpdateTextInputArea — so the rect is set before start on the transition
// frame, and refreshed every frame the session stays up for desktop IMEs.
void sync_text_input_activation(WindowManager& windows, Worlds& worlds) {
    windows.for_each_window([&](WindowId id, WindowSystem& window) {
        ecs::World* const world = worlds.world_for(id);
        const std::optional<ui::TextInputScreenArea> area =
                world != nullptr ? ui::focused_text_input_area(*world, id) : std::nullopt;
        if (area) {
            window.set_text_input_area(area->rect, static_cast<int>(std::lround(area->cursor)));
            if (!window.is_text_input_active()) {
                window.start_text_input();
            }
        } else if (window.is_text_input_active()) {
            window.stop_text_input();
        }
    });
}

class SdlGlPresentation final : public IPresentation {
public:
    SdlGlPresentation() : windows_(*backend_) {
        window_control_ = std::make_unique<WindowControlImpl>(windows_, overlay_, dialogs_);
        window_control_->set_on_windows_changed([this] { sync_modal_hook(); });
    }

    bool init_video() override {
        if (video_inited_) {
            return true;
        }
        // Before SDL_Init. We draw the composition string; the OS keeps the candidate list.
        SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        // A click that also focuses a background window must still deliver BUTTON_DOWN.
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        // Default "0" finishes the Android activity before SDL_SCANCODE_AC_BACK is delivered.
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
        video_inited_ = true;
        return true;
    }

    void shutdown() override {
        windows_.shutdown();
        if (video_inited_) {
            SDL_Quit();
            video_inited_ = false;
        }
    }

    bool create_primary(const WindowDesc& desc) override {
        return windows_.create_primary_window(desc);
    }

    void set_icon(const render::TextureDesc& desc) override {
        windows_.primary_window().set_icon(desc);
    }

    void* native_window() const override {
        return windows_.primary_window().window();
    }

    glm::ivec2 drawable_size() const override {
        return windows_.primary_window().drawable_size();
    }

    std::filesystem::path base_path() const override {
        const char* base = SDL_GetBasePath();
        if (base == nullptr) {
            return {};
        }
        return std::filesystem::path(base);
    }

    render::IGraphicFactory& factory() override {
        return *factory_;
    }

    render::IRenderBackend& backend() override {
        return *backend_;
    }

    render::ICanvas& canvas() override {
        return *windows_.canvas_ptr(kPrimaryWindow);
    }

    render::CommandBuffer& commands() override {
        return *windows_.commands_ptr(kPrimaryWindow);
    }

    render::CommandBuffer* commands_for(WindowId id) override {
        return windows_.commands(id);
    }

    IWindowControl& window_control() override {
        return *window_control_;
    }

    bool add_font(WindowId id, AssetId asset, const Font& font) override {
        render::OpenGLCanvas* const canvas = windows_.canvas(id);
        WindowSystem* const window = windows_.window(id);
        if (canvas == nullptr || window == nullptr) {
            return false;
        }
        // draw_all() leaves whichever window it finished on current. NanoVG font upload needs this one.
        SDL_GL_MakeCurrent(window->window(), canvas->native_context());
        if (asset == builtin::font_ui) {
            return canvas->load_ui_font(font);
        }
        return canvas->add_font(asset, font);
    }

    bool reset_ui_cache(WindowId id) override {
        render::OpenGLCanvas* const canvas = windows_.canvas(id);
        if (canvas == nullptr) {
            return false;
        }
        return canvas->reset_ui_painter();
    }

    bool add_image(WindowId id, AssetId asset, const render::TextureDesc& desc) override {
        render::OpenGLCanvas* const canvas = windows_.canvas(id);
        WindowSystem* const window = windows_.window(id);
        if (canvas == nullptr || window == nullptr) {
            return false;
        }
        SDL_GL_MakeCurrent(window->window(), canvas->native_context());
        return canvas->add_image(asset, desc);
    }

    void poll(Worlds& worlds, InputSystem& input) override {
        worlds.each_world([](ecs::World& world) {
            if (!world.ctx<ui::UiClipboard>().set_text) {
                world.ctx<ui::UiClipboard>() = ui::UiClipboard{
                        .set_text = &sdl_clipboard_set_text,
                        .get_text = &sdl_clipboard_get_text,
                };
            }
        });
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            dispatch(worlds, input, event);
        }
        overlay_.poll_cursor(windows_, input);
        backfill_secondary_sizes(worlds);
        dialogs_->deliver();
    }

    void sync_frame(Worlds& worlds) override {
        overlay_.update_click_through(windows_, worlds.presentation().mouse);
        sync_text_input_activation(windows_, worlds);
    }

    void draw_all(std::span<FrameCapture> captures) override {
        windows_.draw_all(captures);
    }

    void attach_loop(Worlds& worlds, std::function<void()> reentrant_tick) override {
        loop_tick_ = std::move(reentrant_tick);
        publish_primary_size(worlds, false);
        worlds.set_ui_installer([this](ecs::World& world) {
            world.ctx<ui::UiLayoutPainters>().resolve = [this](WindowId id) -> ui::IUiPainter* {
                render::OpenGLCanvas* const canvas = windows_.canvas(id);
                if (canvas == nullptr) {
                    return nullptr;
                }
                canvas->make_current();
                return canvas->ui_painter();
            };
        });
        sync_modal_hook();
    }

    void detach_loop(Worlds& worlds) override {
        loop_tick_ = nullptr;
        sync_modal_hook();
        worlds.set_ui_installer(nullptr);
        worlds.each_world([](ecs::World& world) {
            world.ctx<ui::UiLayoutPainters>().resolve = {};
        });
    }

    void publish_primary_size(Worlds& worlds, bool send_event) override {
        publish_size(worlds, kPrimaryWindow, drawable_size(), send_event);
    }

private:
    void sync_modal_hook() {
        if (loop_tick_) {
            overlay_.sync_modal_loop_hook(windows_, loop_tick_);
        } else {
            overlay_.sync_modal_loop_hook(windows_, nullptr);
        }
    }

    void publish_size(Worlds& worlds, WindowId id, glm::ivec2 size, bool send_event) {
        worlds.presentation().sizes.sizes[id] = ui::WindowSize{size.x, size.y};
        ecs::World* const world = worlds.world_for(id);
        if (world == nullptr) {
            return;
        }
        if (send_event) {
            ecs::EventWriter<ui::WindowResizeEvent>{*world}.send(
                    ui::WindowResizeEvent{.window = id, .width = size.x, .height = size.y});
        }
        ui::apply_canvas_fit(*world);
    }

    void backfill_secondary_sizes(Worlds& worlds) {
        ui::WindowSizes& sizes = worlds.presentation().sizes;
        windows_.for_each_secondary_window([&](WindowId id, WindowSystem& window) {
            if (!sizes.sizes.contains(id)) {
                publish_size(worlds, id, window.drawable_size(), false);
            }
        });
    }

    // The window an event goes to (event_window): an id of a window closed this frame is dropped, so input still
    // queued for a dock float window that just closed does not reach kPrimaryWindow.
    [[nodiscard]] std::optional<WindowId> window_of(SDL_WindowID sdl_id, NoWindowEvent no_window) const {
        return event_window(sdl_id, windows_.find_by_sdl_id(sdl_id), no_window);
    }

    void dispatch(Worlds& worlds, InputSystem& input, const SDL_Event& event) {
        ApplicationState& app = worlds.application_state();
        switch (event.type) {
            case SDL_EVENT_QUIT:
                app.quit();
                break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::WillEnterBackground);
                break;
            case SDL_EVENT_WILL_ENTER_FOREGROUND:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                apply_app_lifecycle(app, AppLifecycleEvent::DidEnterForeground);
                break;
            case SDL_EVENT_TERMINATING:
                apply_app_lifecycle(app, AppLifecycleEvent::Terminating);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                const std::optional<WindowId> resized = window_of(event.window.windowID, NoWindowEvent::Drop);
                if (!resized) {
                    break;
                }
                if (*resized == kPrimaryWindow) {
                    publish_primary_size(worlds, true);
                } else if (WindowSystem* secondary = windows_.window(*resized)) {
                    publish_size(worlds, *resized, secondary->drawable_size(), true);
                }
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                const std::optional<WindowId> closed = window_of(event.window.windowID, NoWindowEvent::Drop);
                if (ecs::World* const world = closed ? worlds.world_for(*closed) : nullptr) {
                    ecs::EventWriter<ui::WindowCloseRequestedEvent>{*world}.send(
                            ui::WindowCloseRequestedEvent{.window = *closed});
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const auto code = static_cast<KeyCode>(static_cast<std::uint32_t>(event.key.scancode));
                const std::optional<WindowId> target = window_of(event.key.windowID, NoWindowEvent::Primary);
                if (!target) {
                    break;
                }
                const WindowId window_id = *target;
                const WindowSystem* window = windows_.window(window_id);
                const bool text_input_active = window != nullptr && window->is_text_input_active();
                switch (back_key_.route(code, event.key.down, event.key.repeat, text_input_active)) {
                    case BackKeyFilter::Route::Deliver:
                        input.handle_key(code, event.key.down, event.key.repeat, window_id);
                        break;
                    case BackKeyFilter::Route::DismissTextInput:
                        if (ecs::World* const world = worlds.world_for(window_id)) {
                            ui::clear_focus(*world, window_id);
                        }
                        break;
                    case BackKeyFilter::Route::Swallow:
                        break;
                }
                break;
            }
            case SDL_EVENT_TEXT_EDITING: {
                const std::optional<WindowId> window_id = window_of(event.edit.windowID, NoWindowEvent::Primary);
                if (!window_id) {
                    break;
                }
                const std::string text = event.edit.text != nullptr ? event.edit.text : "";
                input.handle_text_editing(text, event.edit.start, event.edit.length, *window_id);
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                const std::optional<WindowId> window_id = window_of(event.text.windowID, NoWindowEvent::Primary);
                if (!window_id) {
                    break;
                }
                input.handle_text_input(event.text.text != nullptr ? event.text.text : "", *window_id);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const std::optional<WindowId> target = window_of(event.button.windowID, NoWindowEvent::Primary);
                if (!target) {
                    break;
                }
                const WindowId window_id = *target;
                WindowSystem* window = windows_.window(window_id);
                if (window != nullptr) {
                    if (event.button.down && event.button.button == SDL_BUTTON_LEFT &&
                            window->begin_drag_if_in_region(glm::vec2{event.button.x, event.button.y})) {
                        break;
                    }
                    if (!event.button.down && event.button.button == SDL_BUTTON_LEFT && window->is_dragging()) {
                        window->end_drag();
                        break;
                    }
                }
                input.handle_mouse_button(window_id, mouse_button_from_sdl(event.button.button), event.button.down,
                        glm::vec2{event.button.x, event.button.y}, event.button.clicks);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                const std::optional<WindowId> target = window_of(event.motion.windowID, NoWindowEvent::Primary);
                if (!target) {
                    break;
                }
                const WindowId window_id = *target;
                if (WindowSystem* window = windows_.window(window_id); window != nullptr && window->is_dragging()) {
                    window->update_drag();
                    break;
                }
                input.handle_mouse_move(window_id, glm::vec2{event.motion.x, event.motion.y},
                        glm::vec2{event.motion.xrel, event.motion.yrel});
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                const std::optional<WindowId> target = window_of(event.wheel.windowID, NoWindowEvent::Primary);
                if (!target) {
                    break;
                }
                const WindowId window_id = *target;
                float wheel_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                }
                input.handle_mouse_wheel(window_id, glm::vec2{event.wheel.mouse_x, event.wheel.mouse_y}, wheel_y);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                const std::optional<WindowId> window_id = window_of(event.window.windowID, NoWindowEvent::Drop);
                WindowSystem* const window = window_id ? windows_.window(*window_id) : nullptr;
                if (window != nullptr && window->is_dragging()) {
                    window->end_drag();
                }
                break;
            }
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                input.handle_touch(static_cast<std::uint32_t>(event.tfinger.fingerID),
                        event.type == SDL_EVENT_FINGER_DOWN, pos);
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                const glm::ivec2 size = drawable_size();
                const glm::vec2 pos = denormalize_touch({event.tfinger.x, event.tfinger.y}, size);
                const glm::vec2 rel = denormalize_touch({event.tfinger.dx, event.tfinger.dy}, size);
                input.handle_touch_move(static_cast<std::uint32_t>(event.tfinger.fingerID), pos, rel);
                break;
            }
            default:
                break;
        }
    }

    std::shared_ptr<render::OpenGLFactory> factory_ = std::make_shared<render::OpenGLFactory>();
    std::shared_ptr<render::OpenGLRenderBackend> backend_ = std::make_shared<render::OpenGLRenderBackend>();
    WindowManager windows_;
    DesktopOverlayPolicy overlay_;
    std::shared_ptr<FileDialogCompletions> dialogs_ = std::make_shared<FileDialogCompletions>();
    std::unique_ptr<WindowControlImpl> window_control_;
    BackKeyFilter back_key_;
    bool video_inited_ = false;
    std::function<void()> loop_tick_;
};

}

std::unique_ptr<IPresentation> make_sdl_gl_presentation() {
    return std::make_unique<SdlGlPresentation>();
}

}
