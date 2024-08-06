#include "window_manager.h"

#include "gl_includes.h"

namespace engine {

WindowManager::WindowManager(render::IRenderBackend& backend) : backend_(&backend) {
    // Pre-build the primary slot's CommandBuffer/OpenGLCanvas objects up front, inert until
    // create_primary_window() actually creates the OS window and GL context — see the doc
    // comments on canvas_ptr()/commands_ptr()/primary_window() for why this can't be deferred
    // until the first successful create_primary_window() call.
    auto primary = std::make_unique<Entry>();
    primary->canvas = std::make_shared<render::OpenGLCanvas>(primary->window, *primary->commands, backend);
    windows_[kPrimaryWindow] = std::move(primary);
}

bool WindowManager::create_primary_window(const WindowDesc& desc) {
    // The primary Entry always exists (constructor guarantee) — "re-creating" it is just tearing
    // down and rebuilding its window/context in place rather than swapping in a whole new Entry,
    // so the WindowSystem/CommandBuffer/OpenGLCanvas addresses this manager already handed out via
    // primary_window()/commands_ptr()/canvas_ptr() stay valid across the call.
    Entry& entry = *windows_.at(kPrimaryWindow);
    if (!entry.window.create(desc)) {
        return false;
    }
    return entry.canvas->init(/*with_ui_painter=*/true);
}

std::optional<WindowId> WindowManager::create_window(const WindowDesc& desc) {
    if (!has_window(kPrimaryWindow)) {
        return std::nullopt;
    }
    Entry* primary = windows_.at(kPrimaryWindow).get();

    auto entry = std::make_unique<Entry>();
    if (!entry->window.create(desc)) {
        return std::nullopt;
    }

    // A secondary window's GL context must share the primary's texture/mesh/shader objects
    // (SDD §21.5). SDL_GL_CreateContext (called inside OpenGLCanvas::init() below) consults
    // SDL_GL_SHARE_WITH_CURRENT_CONTEXT against whichever context is current *at that call*, so
    // the primary's context has to be made current here, immediately before init() runs.
    SDL_GL_MakeCurrent(primary->window.window(), primary->canvas->native_context());
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    entry->canvas = std::make_shared<render::OpenGLCanvas>(entry->window, *entry->commands, *backend_);
    // with_ui_painter=true (SDD §21.6): OpenGLCanvas::draw() now re-arms OpenGLRenderBackend's
    // single shared ui_painter_ pointer to *its own* NanoVgPainter immediately before calling
    // execute(), every frame — so the "last window to init() wins" clobbering risk that used to
    // block this (see the removed with_ui_painter=false comment, still described in SDD §21.5) no
    // longer applies: whichever window's draw() ran last simply owns the pointer for the instant
    // its own execute() call actually reads it.
    if (!entry->canvas->init(/*with_ui_painter=*/true)) {
        return std::nullopt;
    }

    const WindowId id{next_id_++};
    windows_[id] = std::move(entry);
    return id;
}

void WindowManager::destroy_window(WindowId id) {
    if (id == kPrimaryWindow) {
        // The primary slot is permanent infrastructure (see the constructor): whether a game may
        // close its own primary window at all is a lifecycle *policy* question deferred to §21.7,
        // not decided by this mechanism-only method. Until that lands, this just tears down the
        // OS window and GL context in place, leaving the slot ready for a later
        // create_primary_window() call, and leaving WindowControlImpl/commands_ptr()/canvas_ptr()
        // pointed at a still-valid (if inert) object.
        Entry& entry = *windows_.at(kPrimaryWindow);
        entry.window.destroy();
        entry.canvas = std::make_shared<render::OpenGLCanvas>(entry.window, *entry.commands, *backend_);
        return;
    }
    windows_.erase(id);
}

void WindowManager::shutdown() {
    // Secondary windows have no long-lived external references in this phase (no per-window DI
    // yet — that's §21.6), so they are fully torn down and forgotten. The primary slot is reset in
    // place via destroy_window() rather than erased, so a later create_window()/
    // create_primary_window() call still works and this manager's already-handed-out primary
    // accessors stay valid — matching the idempotent-shutdown contract EngineRuntime promised
    // before this phase.
    for (auto it = windows_.begin(); it != windows_.end();) {
        if (it->first == kPrimaryWindow) {
            ++it;
        } else {
            it = windows_.erase(it);
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

void WindowManager::draw_all() {
    // The primary slot's canvas object always exists (constructor), even before
    // create_primary_window() ever succeeds — gating on window.window() rather than on canvas
    // non-null avoids issuing raw GL calls through an OpenGLCanvas that was never init()'d (no GL
    // context, no loaded entry points).
    for (auto& [id, entry] : windows_) {
        if (entry->canvas && entry->window.window() != nullptr) {
            entry->canvas->draw();
        }
    }
}

}
