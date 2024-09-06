#pragma once

#include <engine/core/window_desc.h>
#include <engine/render/commands.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine {

// Runtime window control a game asks for through DI (SDD §4.2 — no service locator), backed by
// EngineRuntime's window(s). borderless/always_on_top can change after creation; transparent
// cannot (WindowStyle in window_desc.h, §21.2) so it has no setter here.
class IWindowControl {
public:
    virtual ~IWindowControl() = default;

    // Everything below except open_window/close_window implicitly means kPrimaryWindow (§21.3) —
    // that generalization is unbuilt beyond the two WindowId-addressed methods here, which need a
    // WindowId by their nature (SDD §21.7).
    virtual void set_borderless(bool borderless) = 0;
    virtual void set_always_on_top(bool always_on_top) = 0;
    virtual void set_position(glm::ivec2 position) = 0;
    virtual void resize(glm::ivec2 size) = 0;

    // Manual on/off for §21.4 click-through overlay mode; the automatic per-frame toggle only
    // runs while this is enabled (and the window is transparent).
    virtual void set_click_through_enabled(bool enabled) = 0;

    // Marks a region (window-client pixels, same space as UiCanvas.rect) draggable via
    // SDL_SetWindowHitTest — needed for a borderless window, which has no OS titlebar to drag by
    // (SDD §21.7). nullopt clears it.
    virtual void set_drag_region(std::optional<render::Rect> region) = 0;

    // Opens/closes a secondary window (SDD §21.5/§21.7). nullopt on failure (e.g. no primary
    // window yet). Closing is purely mechanical — see WindowCloseRequestedEvent
    // (include/engine/ui/canvas.h) for how a game learns a window's close button was clicked.
    virtual std::optional<WindowId> open_window(const WindowDesc& desc) = 0;
    virtual void close_window(WindowId id) = 0;
};

}
