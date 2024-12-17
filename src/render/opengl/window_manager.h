#pragma once

#include "opengl_canvas.h"
#include "window_system.h"

#include <engine/core/window_desc.h>
#include <engine/render/backend.h>
#include <engine/render/command_buffer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace engine {

// Owns one {WindowSystem, CommandBuffer, OpenGLCanvas} triple per live WindowId, all sharing the
// one IRenderBackend/IGraphicFactory-produced GL objects EngineRuntime already owns — texture,
// mesh, and shader GL object ids stay valid across the whole GL share group once contexts share,
// so AssetsDb/IGraphicFactory stay single-instance (SDD §21.5). Every window gets its own
// OpenGLCanvas + NanoVgPainter (§21.6) — routing which UiCanvas/CommandBuffer target which window
// is EngineSystemDeps::commands_for_window's job (engine/ecs/systems.h), not this class's.
class WindowManager {
public:
    explicit WindowManager(render::IRenderBackend& backend);
    // Declared (not defaulted) so it can clear the Win32 modal-loop tick hook installed by the
    // constructor (SDD §21.7) on Windows; a no-op body elsewhere. Declaring it unconditionally
    // (rather than only under _WIN32) keeps move-special-member behavior identical across
    // platforms — a destructor guarded by #if would silently suppress the implicit move
    // ctor/assignment on Windows only.
    ~WindowManager();

    [[nodiscard]] bool create_primary_window(const WindowDesc& desc);
    [[nodiscard]] std::optional<WindowId> create_window(const WindowDesc& desc);
    void destroy_window(WindowId id);
    void shutdown();

    // Gated on the window actually being live (a real SDL window exists) — nullptr/false before
    // create_primary_window()/create_window() has succeeded for that id.
    [[nodiscard]] bool has_window(WindowId id) const;
    [[nodiscard]] WindowSystem* window(WindowId id);
    [[nodiscard]] render::OpenGLCanvas* canvas(WindowId id);
    [[nodiscard]] render::CommandBuffer* commands(WindowId id);

    // Ungated: valid whenever a slot for `id` exists at all, regardless of SDL/GL liveness. The
    // primary slot always exists (see constructor), so these are non-null for kPrimaryWindow from
    // the moment the manager is constructed — EngineRuntime needs that to bind
    // commands_ptr()/canvas_ptr() into its DI graph, which happens before create_window() is ever
    // called (Engine<GameT>::init(), engine.h).
    [[nodiscard]] std::shared_ptr<render::OpenGLCanvas> canvas_ptr(WindowId id);
    [[nodiscard]] std::shared_ptr<render::CommandBuffer> commands_ptr(WindowId id);

    // Same reasoning, for IWindowControl: WindowControlImpl is bound over this reference once, at
    // Impl-construction time, before any window exists — its address must stay valid for
    // WindowManager's whole lifetime, which is exactly what the permanent primary slot guarantees.
    [[nodiscard]] WindowSystem& primary_window() noexcept;

    // Draws + swaps every live window (OpenGLCanvas::draw() already swaps at the end) other than
    // `skip`, if given (SDD §21.7/wind-90 — see set_modal_loop_tick_callback's doc comment below
    // for why a reentrant tick wants to skip one specific window's draw/swap).
    void draw_all(std::optional<WindowId> skip = std::nullopt);

    // Resolves an SDL window id (from an SDL_Event's windowID field, §21.6) back to the WindowId
    // that owns it. Linear scan over live windows — window counts are always tiny, so a scan per
    // event is fine and not worth indexing.
    [[nodiscard]] std::optional<WindowId> find_by_sdl_id(SDL_WindowID sdl_id) const;

    // Same idea as find_by_sdl_id, but keyed by the platform-native window handle instead of SDL's
    // own id (SDD §21.7/wind-90) — `native_handle`'s real type is Win32 HWND, passed as void* so
    // this header stays <windows.h>-free (§16 rule 15); window_manager.cpp does the cast. Compares
    // against SDL_PROP_WINDOW_WIN32_HWND_POINTER for each live window, a property that's simply
    // never set on non-Windows platforms — so this is safe (always std::nullopt) to call
    // unconditionally there too, no #if defined(_WIN32) needed on the caller's side.
    [[nodiscard]] std::optional<WindowId> find_by_native_handle(void* native_handle) const;

    // Visits every live (window.window() != nullptr) window other than kPrimaryWindow — mirrors
    // draw_all()'s liveness check. Used by EngineRuntime::tick_loop() to backfill a freshly opened
    // secondary window's WindowSizes entry (SDD §21.7) before that window's first real resize
    // event, if any, arrives.
    void for_each_secondary_window(const std::function<void(WindowId, WindowSystem&)>& fn);

    // Called on every WM_TIMER seen while a Windows modal move/size loop is active — a no-op on
    // other platforms and a no-op here until someone sets it (SDD §21.7 "game freezes during any
    // window drag" fix, extended by wind-89 to a full reentrant tick, not just a redraw). The
    // argument is whichever live window (if any) owns the HWND actually being dragged/resized
    // right now (resolved via find_by_native_handle against the message's hwnd) — wind-90: a
    // window actively being live-moved/resized by the OS turned out to make its own
    // SDL_GL_SwapWindow call block for the DWM compositor to catch up (observed specifically for
    // an opaque borderless window; the transparent/DWM-blur-behind primary overlay didn't exhibit
    // this), which stalled the *entire* reentrant tick — including game logic that has nothing to
    // do with that window — for as long as the block lasted. The callback decides what to do with
    // this; WindowManager only resolves *which* window it is, never *what* runs on the tick or why
    // — it stays ECS-free (SDD §3.4/§4.2). EngineRuntime::begin_loop() supplies the actual callback
    // once its own loop state (IGame&, FixedStepClock, ecs::World&) exists.
    void set_modal_loop_tick_callback(std::function<void(std::optional<WindowId>)> callback) {
        modal_loop_tick_callback_ = std::move(callback);
    }

    [[nodiscard]] const std::function<void(std::optional<WindowId>)>& modal_loop_tick_callback() const noexcept {
        return modal_loop_tick_callback_;
    }

private:
    struct Entry {
        WindowSystem window;
        std::shared_ptr<render::CommandBuffer> commands = std::make_shared<render::CommandBuffer>();
        std::shared_ptr<render::OpenGLCanvas> canvas;   // constructed after window/commands are stable addresses
    };

    render::IRenderBackend* backend_;
    // unique_ptr<Entry>: OpenGLCanvas captures WindowSystem&/CommandBuffer& by reference at
    // construction, so an Entry's address (and its members') must never move — a bare Entry value
    // in the map would dangle on rehash. The kPrimaryWindow entry, once inserted by the
    // constructor, is never erased — only reset in place by create_primary_window()/
    // destroy_window()/shutdown() — precisely so canvas_ptr()/commands_ptr()/primary_window()
    // never dangle (see their doc comments above).
    std::unordered_map<WindowId, std::unique_ptr<Entry>> windows_;
    std::uint32_t next_id_ = 1;   // 0 is kPrimaryWindow, reserved
    std::function<void(std::optional<WindowId>)> modal_loop_tick_callback_;
};

}
