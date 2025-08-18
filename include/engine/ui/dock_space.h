#pragma once

// docs/tech/features/Docking.md#host

#include <engine/builtin_ids.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/dock_geometry.h>
#include <engine/ui/dock_layout.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ecs {
    class World;
}

namespace engine::ui {

    // A panel the dock space places: its layout key, the tab title, and the UiCanvas entity the host spawned for it.
    // The dock system owns that canvas's `rect`, `order`, `window` and `fit` (Fixed) while the panel is registered.
    struct DockPanel {
        std::string key;
        // Tab and float title. Empty shows the key.
        std::string title;
        ecs::Entity canvas{};
        // Its tab shows a close button. A click on it sends DockPanelCloseRequested; the host decides.
        bool closable = false;
    };

    // Where a float of a dock space lives.
    enum class DockFloatMode {
        // A virtual window (frame, title bar, move, resize) inside the space's window, kept inside `area`.
        Virtual,
        // An OS window of its own, bound to the space's world, opened and closed by the dock system through
        // IWindowControl (EngineSystemDeps::windows and ::worlds). The OS title bar and frame replace the virtual ones;
        // the float's tabs and splitters fill the window's client area. Tabs drag between the space's windows. Without
        // window control, or when a window does not open, a float stays virtual.
        OsWindow,
    };

    // A dock space on one window: the docked tree fills `area`; floats live where `float_mode` says. The engine's
    // dock systems draw the chrome (tab strips, splitters, float frames, the drop preview) as UiCanvas entities of
    // their own, handle the pointer on it, and write every registered panel canvas each frame.
    struct DockSpace {
        WindowId window = kPrimaryWindow;
        // Window pixels. The host keeps it current (a window resize, a toolbar).
        render::Rect area{};
        DockMetrics metrics;
        DockLayout layout;
        std::vector<DockPanel> panels;
        // Virtual floats, or one OS window per float. A change converts the floats there are on the next frame.
        DockFloatMode float_mode = DockFloatMode::Virtual;
        // Lowest UiCanvas order the space uses; it takes dock_space_order_count(layout) orders from here.
        int order = 0;
        // Chrome stylesheets, merged in order. The default theme is builtin::dock_css.
        std::vector<AssetId> stylesheets{builtin::dock_css};
        // Pointer travel, in pixels, that turns a tab press into a drag.
        float drag_threshold = 4.0f;
        // Side of a closable tab's close button, inset from the tab's right edge by the same gap as from its top.
        float close_button_size = 14.0f;
        // Bumped each time the dock system changes `layout` (a tab activated, a float raised, a gesture committed, an
        // OS float window moved, resized or closed by the user). A host that saves the layout compares it with the
        // revision it saved. Changes the host makes itself do not bump it.
        std::uint64_t revision = 0;
    };

    // Sent when the close button of a closable tab is clicked. The panel stays until the host removes it.
    struct DockPanelCloseRequested {
        ecs::Entity space{};
        std::string key;
    };

    // UiCanvas orders a space uses from DockSpace::order: docked chrome, docked panels, then chrome and panels of
    // each float bottom to top, then the drag preview.
    [[nodiscard]] inline int dock_space_order_count(const DockLayout &layout) noexcept {
        return 3 + 2 * static_cast<int>(layout.floats().size());
    }

    // The OS window panel `key` of the dock space on entity `space` is shown in: its float's window in
    // DockFloatMode::OsWindow, once the dock system has opened it. nullopt for a docked panel, a virtual float, an
    // unknown key, or an entity without a DockSpace. A host raises it (IWindowControl::raise) to bring the panel to
    // the front; DockLayout::raise_float only orders floats inside the space's window.
    [[nodiscard]] std::optional<WindowId> dock_panel_os_window(ecs::World &world, ecs::Entity space,
                                                               std::string_view key);

    // Window rect of a closable tab's close button.
    [[nodiscard]] render::Rect dock_close_button_rect(const render::Rect &tab, float size) noexcept;

} // namespace engine::ui
