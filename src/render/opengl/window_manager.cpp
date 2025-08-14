#include "window_manager.h"

#include "gl_includes.h"

#include <engine/log.h>

#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace engine {

#if defined(_WIN32)
namespace {

// Fix ("game doesn't update while dragging any window"): on Windows, the OS enters its
// own modal move/size loop inside DefWindowProc as soon as a WM_NCLBUTTONDOWN with HTCAPTION
// arrives — whether HTCAPTION came from a real OS titlebar or from window_drag_hit_test's
// SDL_HITTEST_DRAGGABLE — and the calling thread blocks inside it until the mouse button is
// released. GameLoop's main loop is the classic SDL_PollEvent poll loop (not SDL3's
// SDL_AppIterate/main-callbacks model), so it never runs again until the drag ends: the whole
// game visibly freezes for the duration of any drag, not just ones through a drag region.
// SDL3 already ticks a WM_TIMER (USER_TIMER_MINIMUM, i.e. ~10ms) while inside that modal loop
// (see WM_ENTERSIZEMOVE in SDL_windowsevents.c) purely to drive its own SDL_AppIterate-based main
// loop, which this engine doesn't use — but SDL_SetWindowsMessageHook (SDL_system.h), called for
// every message while the modal loop is active, gives any app a way to piggyback on it. What
// actually runs on each tick isn't this class's business —
// SdlGlPresentation::attach_loop() supplies it via set_modal_loop_tick_callback() (a full
// reentrant game tick, not just a redraw — see GameLoop::reentrant_tick() for
// why that's safe here specifically).
bool windows_message_hook(void* userdata, MSG* msg) {
    if (msg != nullptr && msg->message == WM_TIMER) {
        auto* self = static_cast<WindowManager*>(userdata);
        if (const auto& callback = self->modal_loop_tick_callback()) {
            callback();
        }
    }
    // Must always return true: SDL_windowsevents.c drops the message entirely (WIN_WindowProc
    // returns 0 without further processing) whenever a Windows message hook returns false.
    return true;
}

}
#endif

WindowManager::WindowManager(render::IRenderBackend& backend) : backend_(&backend) {
    // Pre-build the primary slot's CommandBuffer/OpenGLCanvas objects up front, inert until
    // create_primary_window() actually creates the OS window and GL context — see the doc
    // comments on canvas_ptr()/commands_ptr()/primary_window() for why this can't be deferred
    // until the first successful create_primary_window() call.
    auto primary = std::make_unique<Entry>();
    primary->canvas = std::make_shared<render::OpenGLCanvas>(primary->window, *primary->commands, backend);
    windows_[kPrimaryWindow] = std::move(primary);
}

WindowManager::~WindowManager() {
    set_modal_loop_tick_callback(nullptr);
}

void WindowManager::set_modal_loop_tick_callback(std::function<void()> callback) {
    modal_loop_tick_callback_ = std::move(callback);
#if defined(_WIN32)
    if (modal_loop_tick_callback_) {
        // only hook into Windows messages when a modal loop tick callback is actually
        // active (e.g. desktop overlay mode), avoiding process-global message interception
        // overhead for normal games.
        SDL_SetWindowsMessageHook(&windows_message_hook, this);
    } else {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
    }
#endif
}

bool WindowManager::create_primary_window(const WindowDesc& desc) {
    // The primary Entry always exists (constructor guarantee) — "re-creating" it is just tearing
    // down and rebuilding its window/context in place rather than swapping in a whole new Entry,
    // so the WindowSystem/CommandBuffer/OpenGLCanvas addresses this manager already handed out via
    // primary_window()/commands_ptr()/canvas_ptr() stay valid across the call.
    if (desc.owner) {
        log::error("Window: the primary window cannot have an owner");
        return false;
    }
    Entry& entry = *windows_.at(kPrimaryWindow);
    if (!entry.window.create(desc, nullptr)) {
        return false;
    }
    if (!entry.canvas->init(/*with_ui_painter=*/true)) {
        return false;
    }
    init_swap_interval(kPrimaryWindow, entry);
    return true;
}

std::optional<WindowId> WindowManager::create_window(const WindowDesc& desc) {
    if (!has_window(kPrimaryWindow)) {
        return std::nullopt;
    }
    Entry* primary = windows_.at(kPrimaryWindow).get();

    SDL_Window* owner = nullptr;
    if (desc.owner) {
        WindowSystem* const owner_window = window(*desc.owner);
        if (owner_window == nullptr) {
            return std::nullopt;
        }
        owner = owner_window->window();
    }

    auto entry = std::make_unique<Entry>();
    entry->owner = desc.owner;
    if (!entry->window.create(desc, owner)) {
        return std::nullopt;
    }

    // A secondary window's GL context must share the primary's texture/mesh/shader objects.
    // SDL_GL_CreateContext (called inside OpenGLCanvas::init() below) consults
    // SDL_GL_SHARE_WITH_CURRENT_CONTEXT against whichever context is current *at that call*, so
    // the primary's context has to be made current here, immediately before init() runs.
    SDL_GL_MakeCurrent(primary->window.window(), primary->canvas->native_context());
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    entry->canvas = std::make_shared<render::OpenGLCanvas>(entry->window, *entry->commands, *backend_);
    // with_ui_painter=true: OpenGLCanvas::draw() now re-arms OpenGLRenderBackend's
    // single shared ui_painter_ pointer to *its own* NanoVgPainter immediately before calling
    // execute(), every frame — so the "last window to init() wins" clobbering risk that used to
    // block this no longer applies: whichever window's draw() ran last simply owns the pointer for the instant
    // its own execute() call actually reads it.
    if (!entry->canvas->init(/*with_ui_painter=*/true)) {
        return std::nullopt;
    }

    const WindowId id{next_id_++};
    init_swap_interval(id, *entry);
    windows_[id] = std::move(entry);
    return id;
}

void WindowManager::destroy_window(WindowId id) {
    // SDL_DestroyWindow destroys the windows a window owns before it; closing them here first keeps no entry
    // holding an SDL window SDL already freed.
    std::vector<WindowId> owned;
    for (const auto& [other, entry] : windows_) {
        if (entry->owner == id && entry->window.window() != nullptr) {
            owned.push_back(other);
        }
    }
    for (const WindowId other : owned) {
        destroy_window(other);
    }
    if (vsync_window_ == id) {
        vsync_window_.reset();
    }
    if (id == kPrimaryWindow) {
        // The primary slot is permanent infrastructure (see the constructor): whether a game may
        // close its own primary window at all is a lifecycle *policy* question,
        // not decided by this mechanism-only method. Until a product rule lands, this just tears down the
        // OS window and GL context in place, leaving the slot ready for a later
        // create_primary_window() call, and leaving WindowControlImpl/commands_ptr()/canvas_ptr()
        // pointed at a still-valid (if inert) object.
        Entry& entry = *windows_.at(kPrimaryWindow);
        // The old canvas goes first, while its SDL window still exists: its teardown makes its own context current.
        entry.canvas = std::make_shared<render::OpenGLCanvas>(entry.window, *entry.commands, *backend_);
        entry.window.destroy();
        return;
    }
    windows_.erase(id);
}

void WindowManager::shutdown() {
    set_modal_loop_tick_callback(nullptr);
    vsync_window_.reset();
    // Secondary windows are fully torn down and forgotten. The primary slot is reset in
    // place via destroy_window() rather than erased, so a later create_window()/
    // create_primary_window() call still works and this manager's already-handed-out primary
    // accessors stay valid — matching the idempotent-shutdown contract EngineRuntime promised
    // before this phase.
    // Through destroy_window, so an owned window closes before its owner.
    std::vector<WindowId> secondary;
    for (const auto& [id, entry] : windows_) {
        if (id != kPrimaryWindow) {
            secondary.push_back(id);
        }
    }
    for (const WindowId id : secondary) {
        if (windows_.contains(id)) {
            destroy_window(id);
        }
    }
    destroy_window(kPrimaryWindow);
    // next_id_ is intentionally not reset: keeps window ids from ever being reused across a
    // shutdown/recreate cycle within one process, matching this codebase's "never reuse a GUID"
    // hygiene for asset ids, applied here by analogy.
}

bool WindowManager::has_window(WindowId id) const {
    const auto it = windows_.find(id);
    return it != windows_.end() && it->second->window.window() != nullptr;
}

WindowSystem* WindowManager::window(WindowId id) {
    return has_window(id) ? &windows_.at(id)->window : nullptr;
}

const WindowSystem* WindowManager::window(WindowId id) const {
    return has_window(id) ? &windows_.at(id)->window : nullptr;
}

render::OpenGLCanvas* WindowManager::canvas(WindowId id) {
    return has_window(id) ? windows_.at(id)->canvas.get() : nullptr;
}

render::CommandBuffer* WindowManager::commands(WindowId id) {
    return has_window(id) ? windows_.at(id)->commands.get() : nullptr;
}

std::shared_ptr<render::OpenGLCanvas> WindowManager::canvas_ptr(WindowId id) {
    const auto it = windows_.find(id);
    return it == windows_.end() ? nullptr : it->second->canvas;
}

std::shared_ptr<render::CommandBuffer> WindowManager::commands_ptr(WindowId id) {
    const auto it = windows_.find(id);
    return it == windows_.end() ? nullptr : it->second->commands;
}

WindowSystem& WindowManager::primary_window() noexcept {
    return windows_.at(kPrimaryWindow)->window;
}

const WindowSystem& WindowManager::primary_window() const noexcept {
    return windows_.at(kPrimaryWindow)->window;
}

std::optional<WindowId> WindowManager::find_by_sdl_id(SDL_WindowID sdl_id) const {
    for (const auto& [id, entry] : windows_) {
        SDL_Window* sdl_window = entry->window.window();
        if (sdl_window != nullptr && SDL_GetWindowID(sdl_window) == sdl_id) {
            return id;
        }
    }
    return std::nullopt;
}

void WindowManager::for_each_secondary_window(const std::function<void(WindowId, WindowSystem&)>& fn) {
    for (auto& [id, entry] : windows_) {
        if (id != kPrimaryWindow && entry->window.window() != nullptr) {
            fn(id, entry->window);
        }
    }
}

void WindowManager::for_each_window(const std::function<void(WindowId, WindowSystem&)>& fn) {
    for (auto& [id, entry] : windows_) {
        if (entry->window.window() != nullptr) {
            fn(id, entry->window);
        }
    }
}

void WindowManager::for_each_window(const std::function<void(WindowId, const WindowSystem&)>& fn) const {
    for (const auto& [id, entry] : windows_) {
        if (entry->window.window() != nullptr) {
            fn(id, entry->window);
        }
    }
}

void WindowManager::init_swap_interval(WindowId id, Entry& entry) {
    // A recreated primary has a new context with the driver's default interval, so the vsync window is chosen
    // again on the next frame.
    if (vsync_window_ == id) {
        vsync_window_.reset();
    }
#if defined(__EMSCRIPTEN__)
    // SDL's Emscripten swap interval sets the main loop's timing; GameLoop already runs on requestAnimationFrame.
    entry.vsync_supported = false;
#else
    entry.vsync_supported = entry.canvas->set_vsync(false);
#endif
}

std::optional<WindowId> WindowManager::sync_vsync_window() {
    pacing_.clear();
    for (const auto& [id, entry] : windows_) {
        if (entry->window.window() != nullptr) {
            pacing_.push_back(PacingWindow{
                    .id = id,
                    .presentable = entry->window.is_presentable(),
                    .vsync_supported = entry->vsync_supported,
            });
        }
    }
    const std::optional<WindowId> chosen = vsync_ ? choose_vsync_window(pacing_) : std::nullopt;
    if (chosen == vsync_window_) {
        return vsync_window_;
    }
    if (vsync_window_) {
        if (render::OpenGLCanvas* const previous = canvas(*vsync_window_)) {
            (void)previous->set_vsync(false);
        }
        vsync_window_.reset();
    }
    if (chosen) {
        Entry& entry = *windows_.at(*chosen);
        if (entry.canvas->set_vsync(true)) {
            vsync_window_ = chosen;
        } else {
            entry.vsync_supported = false;
        }
    }
    return vsync_window_;
}

void WindowManager::wait_for_next_frame(bool vsync_waited) {
#if defined(__EMSCRIPTEN__)
    (void)vsync_waited;
#else
    const std::optional<std::chrono::nanoseconds> period =
            limiter_period(vsync_, vsync_waited, max_fps_, primary_window().refresh_rate());
    if (!period) {
        return;
    }
    const std::chrono::nanoseconds wait = limiter_.wait_after(FrameLimiter::Clock::now(), *period);
    if (wait > std::chrono::nanoseconds::zero()) {
        SDL_DelayPrecise(static_cast<Uint64>(wait.count()));
    }
#endif
}

void WindowManager::draw_window(WindowId id, Entry& entry, std::span<FrameCapture> captures) {
    entry.canvas->render();
    for (FrameCapture& capture : captures) {
        if (capture.window == id) {
            capture.image = entry.canvas->read_pixels();
        }
    }
    entry.canvas->present();
}

void WindowManager::draw_all(std::span<FrameCapture> captures) {
    // The primary slot's canvas object always exists (constructor), even before
    // create_primary_window() ever succeeds — gating on window.window() rather than on canvas
    // non-null avoids issuing raw GL calls through an OpenGLCanvas that was never init()'d (no GL
    // context, no loaded entry points).
    const std::optional<WindowId> vsync = sync_vsync_window();
    for (auto& [id, entry] : windows_) {
        if (id != vsync && entry->canvas && entry->window.window() != nullptr) {
            draw_window(id, *entry, captures);
        }
    }
    if (vsync) {
        draw_window(*vsync, *windows_.at(*vsync), captures);
    }
    wait_for_next_frame(vsync.has_value());
}

}
