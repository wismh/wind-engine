#pragma once

// docs/tech/features/Windowing.md

#include <engine/core/file_dialog.h>
#include <engine/core/window_desc.h>
#include <engine/render/commands.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace engine {

// Selects whether desktop-overlay behaviors run at all: synthetic cursor polling for
// click-through windows, per-window WS_EX_TRANSPARENT sync, and the Win32 modal-loop reentrant
// tick hook. Auto (the default) infers this from whether any live window is transparent — a game
// that wants an alpha-blended window without any of those OS hooks sets AlwaysDisabled; one that
// wants them running even before a transparent window exists yet sets AlwaysEnabled. Engine-wide,
// not per-window: one policy backs every window a game opens.
enum class OverlayMode {
    Auto,
    AlwaysEnabled,
    AlwaysDisabled
};

// Runtime window control a game asks for through DI (no service locator), backed by
// EngineRuntime's window(s). borderless/always_on_top can change after creation; transparent
// cannot (WindowStyle in window_desc.h) so it has no setter here.
class IWindowControl {
public:
    virtual ~IWindowControl() = default;

    // See OverlayMode above. Call before opening a transparent window if the game needs
    // AlwaysDisabled from the start — Auto would otherwise activate overlay hooks the instant that
    // window is created.
    virtual void set_overlay_mode(OverlayMode mode) = 0;
    [[nodiscard]] virtual OverlayMode overlay_mode() const = 0;

    // Frame pacing for the whole process, not per window. Vsync on (the default): one window's swap waits for
    // the display, so frames run at its refresh rate. Off: no swap waits, and frames run as fast as they can
    // unless max_fps caps them. Web runs on requestAnimationFrame and ignores both.
    virtual void set_vsync(bool enabled) = 0;
    [[nodiscard]] virtual bool vsync() const = 0;

    // Most frames per second while no swap waits for vsync: vsync off, or on while every window is hidden or
    // minimized (then the display's refresh rate is the limit, and a lower cap still applies). Ignored while
    // a vsync swap paces the frame. 0, the default, is no cap; a negative value counts as 0.
    virtual void set_max_fps(int fps) = 0;
    [[nodiscard]] virtual int max_fps() const = 0;

    // `window` (default kPrimaryWindow, trailing so every pre-existing call site keeps compiling
    // and behaving unchanged) generalizes these to the secondary windows opened via open_window()
    // below — a settings window can now toggle its own always_on_top or reposition
    // itself, not just the primary. A `window` with no live SDL window (not yet opened, or already
    // closed) makes these a no-op, same contract as calling them before the primary window exists.
    virtual void set_title(std::string_view title, WindowId window = kPrimaryWindow) = 0;
    virtual void set_borderless(bool borderless, WindowId window = kPrimaryWindow) = 0;
    virtual void set_always_on_top(bool always_on_top, WindowId window = kPrimaryWindow) = 0;
    virtual void set_position(glm::ivec2 position, WindowId window = kPrimaryWindow) = 0;
    virtual void resize(glm::ivec2 size, WindowId window = kPrimaryWindow) = 0;

    // Brings the window to the front and gives it keyboard focus; a minimized window is restored first. A window
    // that is not open is a no-op. The OS may refuse focus to a process that is not in the foreground (Windows
    // then flashes its taskbar entry, or its owner's).
    virtual void raise(WindowId window) = 0;

    // Live top-left in screen coordinates, the same space as set_position. nullopt if that
    // window is not open.
    [[nodiscard]] virtual std::optional<glm::ivec2> position(WindowId window = kPrimaryWindow) const = 0;

    // Live client size in screen coordinates, the same space as resize. nullopt if that window
    // is not open. This is not the drawable pixel size on Presentation::sizes.
    [[nodiscard]] virtual std::optional<glm::ivec2> size(WindowId window = kPrimaryWindow) const = 0;

    // Manual on/off for click-through overlay mode; the automatic per-frame toggle only
    // runs while this is enabled (and the window is transparent). `window` generalizes to secondary
    // windows the same way as other controls.
    virtual void set_click_through_enabled(bool enabled, WindowId window = kPrimaryWindow) = 0;

    // Marks a region (window-client pixels, same coordinate space as UiCanvas.rect) draggable —
    // needed for a borderless window, which has no OS titlebar to drag by. nullopt
    // clears it. `window` generalizes to secondary windows the same way as other controls.
    //
    // Window movement is handled manually by the engine via mouse capture (SDL_CaptureMouse),
    // avoiding the OS's native modal move loop (DefWindowProc / HTCAPTION) and its associated
    // message starvation.
    //
    // WARNING: this is a raw geometry rectangle evaluated before UI event dispatch. Any left click
    // inside it initiates window dragging and is consumed immediately by the engine — no
    // button-down event reaches the UI or ECS input systems. Consequently, a Button placed inside
    // the drag rect (e.g. a titlebar close button next to or inside a drag handle) is unclickable.
    // Shrink or notch the rect to exclude every interactive control's bounds before calling this.
    virtual void set_drag_region(std::optional<render::Rect> region, WindowId window = kPrimaryWindow) = 0;

    // Opens/closes a secondary window. nullopt on failure (e.g. no primary
    // window yet, or a WindowDesc::owner that is not open). Closing is purely mechanical — see
    // WindowCloseRequestedEvent (include/engine/ui/canvas.h) for how a game learns a window's close button was
    // clicked. Closing a window closes the windows it owns (WindowDesc::owner) first.
    virtual std::optional<WindowId> open_window(const WindowDesc& desc) = 0;
    virtual void close_window(WindowId id) = 0;

    // Every live window, kPrimaryWindow included once it exists. Order is unspecified.
    [[nodiscard]] virtual std::vector<WindowId> open_windows() const = 0;

    // Shows the platform's open-file dialog, modal to `owner` where the platform supports it, and
    // returns at once. The caller owns the returned call (file_dialog.h); its answer becomes visible
    // during the poll of some later frame. Dropping the call drops the answer, but the dialog stays open
    // until the user closes it. An empty `filters` list shows every file.
    [[nodiscard]] virtual FileDialogCall request_open_file(WindowId owner, std::vector<FileFilter> filters) = 0;

    // The platform's choose-folder dialog, the same way: the answer's `path` is the chosen directory.
    // `start` is the directory it opens in; empty, or one that does not exist, leaves that to the platform.
    [[nodiscard]] virtual FileDialogCall request_open_folder(WindowId owner, std::filesystem::path start) = 0;

    // The display's usable area in screen pixels — the full display bounds minus OS chrome
    // (Windows taskbar, macOS menu bar/dock) — so a game can place a fixed-size overlay flush
    // against a screen edge without hardcoding a platform-specific work-area query itself.
    // `display_index` is 0-based into the platform's display list; out of range falls back
    // to the primary display. A zeroed Rect means the query failed (e.g. no video subsystem).
    [[nodiscard]] virtual render::Rect usable_display_bounds(int display_index = 0) const = 0;

    // Usable area, in screen pixels, of the display that currently contains `window`.
    // A window that is not open, or that cannot be placed on a display, uses the primary
    // display — the same result as usable_display_bounds(0).
    [[nodiscard]] virtual render::Rect usable_display_bounds_for_window(WindowId window = kPrimaryWindow) const = 0;
};

}
