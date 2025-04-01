#include "sdl_gl_presentation.h"

#include "clipboard.h"
#include "opengl_backend.h"
#include "opengl_canvas.h"
#include "opengl_factory.h"
#include "window_control.h"
#include "window_manager.h"

#include "ui/painter.h"

#include <engine/builtin_ids.h>
#include <engine/core/app_lifecycle.h>
#include <engine/core/input_system.h>
#include <engine/core/key_code.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/resources/font.h>
#include <engine/ui/canvas.h>

#include <SDL3/SDL.h>

#include <cstdint>
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

// Starts/stops each window's SDL text-input session to match UiFocusState. Without this,
// SDL_EVENT_TEXT_INPUT never fires and a focused TextInput only blinks its caret.
void sync_text_input_activation(WindowManager& windows, ecs::World& world) {
    const auto& focus_state = world.ctx<ui::UiFocusState>();
    windows.for_each_window([&](WindowId id, WindowSystem& window) {
        const auto it = focus_state.focused.find(id);
        const bool wants_text = it != focus_state.focused.end() && it->second.element != nullptr;
        if (wants_text != window.is_text_input_active()) {
            if (wants_text) {
                window.start_text_input();
            } else {
                window.stop_text_input();
            }
        }
    });
}

class SdlGlPresentation final : public IPresentation {
public:
    SdlGlPresentation() : windows_(*backend_) {
        window_control_ = std::make_unique<WindowControlImpl>(windows_, overlay_);
        window_control_->set_on_windows_changed([this] { sync_modal_hook(); });
    }

    bool init_video() override {
        if (video_inited_) {
            return true;
        }
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

    bool add_image(WindowId id, AssetId asset, const render::TextureDesc& desc) override {
        render::OpenGLCanvas* const canvas = windows_.canvas(id);
        WindowSystem* const window = windows_.window(id);
        if (canvas == nullptr || window == nullptr) {
            return false;
        }
        SDL_GL_MakeCurrent(window->window(), canvas->native_context());
        return canvas->add_image(asset, desc);
    }

    void poll(ecs::World& world, InputSystem& input, ApplicationState& app) override {
        if (!world.ctx<ui::UiClipboard>().set_text) {
            world.ctx<ui::UiClipboard>() = ui::UiClipboard{
                    .set_text = &sdl_clipboard_set_text,
                    .get_text = &sdl_clipboard_get_text,
            };
        }
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            dispatch(world, input, app, event);
        }
        overlay_.poll_cursor(windows_, input);
        backfill_secondary_sizes(world);
    }

    void sync_frame(ecs::World& world) override {
        overlay_.update_click_through(windows_, world.ctx<ui::MouseConsumed>());
        sync_text_input_activation(windows_, world);
    }

    void draw_all() override {
        windows_.draw_all();
    }

    void attach_loop(ecs::World& world, std::function<void()> reentrant_tick) override {
        loop_tick_ = std::move(reentrant_tick);
        publish_primary_size(world, false);
        world.ctx<ui::UiLayoutPainters>().resolve = [this](WindowId id) -> ui::IUiPainter* {
            render::OpenGLCanvas* const canvas = windows_.canvas(id);
            if (canvas == nullptr) {
                return nullptr;
            }
            canvas->make_current();
            return canvas->ui_painter();
        };
        sync_modal_hook();
    }

    void detach_loop(ecs::World& world) override {
        loop_tick_ = nullptr;
        sync_modal_hook();
        world.ctx<ui::UiLayoutPainters>().resolve = {};
    }

    void publish_primary_size(ecs::World& world, bool send_event) override {
        const glm::ivec2 size = drawable_size();
        world.ctx<ui::WindowSizes>().sizes[kPrimaryWindow] = ui::WindowSize{size.x, size.y};
        if (send_event) {
            ecs::EventWriter<ui::WindowResizeEvent>{world}.send(
                    ui::WindowResizeEvent{.window = kPrimaryWindow, .width = size.x, .height = size.y});
        }
        ui::apply_canvas_fit(world);
    }

private:
    void sync_modal_hook() {
        if (loop_tick_) {
            overlay_.sync_modal_loop_hook(windows_, loop_tick_);
        } else {
            overlay_.sync_modal_loop_hook(windows_, nullptr);
        }
    }

    void backfill_secondary_sizes(ecs::World& world) {
        ui::WindowSizes& sizes = world.ctx<ui::WindowSizes>();
        bool backfilled = false;
        windows_.for_each_secondary_window([&](WindowId id, WindowSystem& window) {
            if (!sizes.sizes.contains(id)) {
                const glm::ivec2 size = window.drawable_size();
                sizes.sizes[id] = ui::WindowSize{size.x, size.y};
                backfilled = true;
            }
        });
        if (backfilled) {
            ui::apply_canvas_fit(world);
        }
    }

    void dispatch(ecs::World& world, InputSystem& input, ApplicationState& app, const SDL_Event& event) {
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
                const WindowId resized = windows_.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                if (resized == kPrimaryWindow) {
                    publish_primary_size(world, true);
                } else if (WindowSystem* secondary = windows_.window(resized)) {
                    const glm::ivec2 size = secondary->drawable_size();
                    world.ctx<ui::WindowSizes>().sizes[resized] = ui::WindowSize{size.x, size.y};
                    ecs::EventWriter<ui::WindowResizeEvent>{world}.send(
                            ui::WindowResizeEvent{.window = resized, .width = size.x, .height = size.y});
                    ui::apply_canvas_fit(world);
                }
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                const WindowId closed = windows_.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                ecs::EventWriter<ui::WindowCloseRequestedEvent>{world}.send(
                        ui::WindowCloseRequestedEvent{.window = closed});
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const auto code = static_cast<KeyCode>(static_cast<std::uint32_t>(event.key.scancode));
                const WindowId window_id = windows_.find_by_sdl_id(event.key.windowID).value_or(kPrimaryWindow);
                input.handle_key(code, event.key.down, event.key.repeat, window_id);
                if (event.key.down && !event.key.repeat && code == KeyCode::AcBack) {
                    const WindowSystem* window = windows_.window(window_id);
                    const bool text_input_active = window != nullptr && window->is_text_input_active();
                    if (text_input_active) {
                        ui::clear_focus(world, window_id);
                    }
                    apply_android_back(app, text_input_active);
                }
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                const WindowId window_id = windows_.find_by_sdl_id(event.text.windowID).value_or(kPrimaryWindow);
                input.handle_text_input(event.text.text != nullptr ? event.text.text : "", window_id);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const WindowId window_id = windows_.find_by_sdl_id(event.button.windowID).value_or(kPrimaryWindow);
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
                const WindowId window_id = windows_.find_by_sdl_id(event.motion.windowID).value_or(kPrimaryWindow);
                if (WindowSystem* window = windows_.window(window_id); window != nullptr && window->is_dragging()) {
                    window->update_drag();
                    break;
                }
                input.handle_mouse_move(window_id, glm::vec2{event.motion.x, event.motion.y},
                        glm::vec2{event.motion.xrel, event.motion.yrel});
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                const WindowId window_id = windows_.find_by_sdl_id(event.wheel.windowID).value_or(kPrimaryWindow);
                float wheel_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                }
                input.handle_mouse_wheel(window_id, glm::vec2{event.wheel.mouse_x, event.wheel.mouse_y}, wheel_y);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                const WindowId window_id = windows_.find_by_sdl_id(event.window.windowID).value_or(kPrimaryWindow);
                if (WindowSystem* window = windows_.window(window_id); window != nullptr && window->is_dragging()) {
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
    std::unique_ptr<WindowControlImpl> window_control_;
    bool video_inited_ = false;
    std::function<void()> loop_tick_;
};

}

std::unique_ptr<IPresentation> make_sdl_gl_presentation() {
    return std::make_unique<SdlGlPresentation>();
}

}
