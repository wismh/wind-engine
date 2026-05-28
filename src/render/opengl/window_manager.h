#pragma once

#include "opengl_canvas.h"
#include "window_system.h"

#include "core/frame_limiter.h"
#include "core/frame_pacing.h"

#include <engine/core/window_desc.h>
#include <engine/render/backend.h>
#include <engine/render/command_buffer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine {

// Owns one {WindowSystem, CommandBuffer, OpenGLCanvas} triple per live WindowId, all sharing the
// one IRenderBackend/IGraphicFactory-produced GL objects EngineRuntime already owns — texture,
// mesh, and shader GL object ids stay valid across the whole GL share group once contexts share,
// so AssetsDb/IGraphicFactory stay single-instance. Every window gets its own
// OpenGLCanvas + NanoVgPainter — routing which UiCanvas/CommandBuffer target which window
// is EngineSystemDeps::commands_for_window's job (engine/ecs/systems.h), not this class's.
class WindowManager {
public:
    explicit WindowManager(render::IRenderBackend& backend);
    // Declared (not defaulted) so it can clear the Win32 modal-loop tick hook installed by the
    // constructor on Windows; a no-op body elsewhere. Declaring it unconditionally
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
    [[nodiscard]] const WindowSystem* window(WindowId id) const;
    [[nodiscard]] render::OpenGLCanvas* canvas(WindowId id);
    [[nodiscard]] render::CommandBuffer* commands(WindowId id);

    // Ungated: valid whenever a slot for `id` exists at all, regardless of SDL/GL liveness. The
    // primary slot always exists (see constructor), so these are non-null for kPrimaryWindow from
    // the moment the manager is constructed — Engine::init reads commands()/canvas() before
    // create_window() runs.
    [[nodiscard]] std::shared_ptr<render::OpenGLCanvas> canvas_ptr(WindowId id);
    [[nodiscard]] std::shared_ptr<render::CommandBuffer> commands_ptr(WindowId id);

    // Same reasoning, for IWindowControl: WindowControlImpl is bound over this reference once, at
    // Impl-construction time, before any window exists — its address must stay valid for
    // WindowManager's whole lifetime, which is exactly what the permanent primary slot guarantees.
    [[nodiscard]] WindowSystem& primary_window() noexcept;
    [[nodiscard]] const WindowSystem& primary_window() const noexcept;

    // Draws + swaps every live window (OpenGLCanvas::draw() already swaps at the end), then waits for the next
    // frame once: the vsync window's swap waits for vblank and goes last, every other window swaps without
    // waiting, and with no vsync window FrameLimiter sleeps (limiter_period). Web is paced by
    // requestAnimationFrame and never waits here.
    void draw_all();

    // IWindowControl::set_vsync / set_max_fps. Applied by the next draw_all.
    void set_vsync(bool enabled) noexcept {
        vsync_ = enabled;
    }
    [[nodiscard]] bool vsync() const noexcept {
        return vsync_;
    }
    void set_max_fps(int fps) noexcept {
        max_fps_ = fps > 0 ? fps : 0;
    }
    [[nodiscard]] int max_fps() const noexcept {
        return max_fps_;
    }

    // Resolves an SDL window id (from an SDL_Event's windowID field) back to the WindowId
    // that owns it. Linear scan over live windows — window counts are always tiny, so a scan per
    // event is fine.
    [[nodiscard]] std::optional<WindowId> find_by_sdl_id(SDL_WindowID sdl_id) const;

    // Visits every live (window.window() != nullptr) window other than kPrimaryWindow — mirrors
    // draw_all()'s liveness check. SdlGlPresentation::poll uses this to backfill a freshly opened
    // secondary window's WindowSizes entry before that window's first real resize event, if any, arrives.
    void for_each_secondary_window(const std::function<void(WindowId, WindowSystem&)>& fn);

    // Visits every live (window.window() != nullptr) window, including kPrimaryWindow.
    void for_each_window(const std::function<void(WindowId, WindowSystem&)>& fn);
    void for_each_window(const std::function<void(WindowId, const WindowSystem&)>& fn) const;

    // Called on every WM_TIMER seen while a Windows modal move/size loop is active — a no-op on
    // other platforms and a no-op here until someone sets it.
    // SdlGlPresentation::attach_loop() supplies the callback once GameLoop is running.
    void set_modal_loop_tick_callback(std::function<void()> callback);

    [[nodiscard]] const std::function<void()>& modal_loop_tick_callback() const noexcept {
        return modal_loop_tick_callback_;
    }

private:
    struct Entry {
        WindowSystem window;
        std::shared_ptr<render::CommandBuffer> commands = std::make_shared<render::CommandBuffer>();
        std::shared_ptr<render::OpenGLCanvas> canvas;   // constructed after window/commands are stable addresses
        // False once the driver refused this context's swap interval; such a window never paces.
        bool vsync_supported = false;
    };

    // Turns vsync off on a fresh context, whatever the driver's default is, so only the vsync window waits.
    void init_swap_interval(WindowId id, Entry& entry);
    // Moves vsync to the window choose_vsync_window picks this frame and returns it.
    std::optional<WindowId> sync_vsync_window();
    void wait_for_next_frame(bool vsync_waited);

    render::IRenderBackend* backend_;
    // unique_ptr<Entry>: OpenGLCanvas captures WindowSystem&/CommandBuffer& by reference at
    // construction, so an Entry's address (and its members') must never move — a bare Entry value
    // in the map would dangle on rehash. The kPrimaryWindow entry, once inserted by the
    // constructor, is never erased — only reset in place by create_primary_window()/
    // destroy_window()/shutdown() — precisely so canvas_ptr()/commands_ptr()/primary_window()
    // never dangle (see their doc comments above).
    std::unordered_map<WindowId, std::unique_ptr<Entry>> windows_;
    std::uint32_t next_id_ = 1;   // 0 is kPrimaryWindow, reserved
    std::function<void()> modal_loop_tick_callback_;
    bool vsync_ = true;
    int max_fps_ = 0;
    std::optional<WindowId> vsync_window_;
    std::vector<PacingWindow> pacing_;   // reused by sync_vsync_window every frame
    FrameLimiter limiter_;
};

}
