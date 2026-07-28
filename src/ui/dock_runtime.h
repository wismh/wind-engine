#pragma once

// docs/tech/features/Docking.md#host — engine-private state of the dock systems.

#include <engine/core/input_system.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_geometry.h>
#include <engine/ui/dock_space.h>

#include "ui/dock_chrome.h"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace engine::ui {

    // One chrome canvas the dock system spawned and owns.
    struct DockChromeCanvas {
        ecs::Entity canvas{};
        std::shared_ptr<DockChromeViewModel> vm;
    };

    enum class DockGestureKind {
        None,
        // Button down on a tab, not yet past the drag threshold.
        TabPress,
        TabDrag,
        Splitter,
        FloatMove,
        FloatResize,
    };

    struct DockGesture {
        DockGestureKind kind = DockGestureKind::None;
        // The window the press was in. The OS keeps sending that window the pointer while the button is down (SDL
        // auto-capture), outside its client area too, so every position of the gesture is in its client pixels.
        WindowId window = kPrimaryWindow;
        std::string key;
        // The split or the float.
        DockNodeId node = kNoDockNode;
        glm::vec2 press{};
        // Tab drag: where the pointer sits inside the frame of a float the panel would make.
        glm::vec2 grab{};
        // Float move and resize: the frame as shown at the press (at its home), and as stored, which cancel puts back.
        render::Rect start_rect{};
        render::Rect stored_rect{};
        float start_ratio = 0.5f;
        DockSplitterRect splitter{};
        DockResizeEdges edges{};
        // While a tab or a float is dragged: what release does, and the preview. A Float drop's preview is the new
        // float's rect in the space's window, which is what release stores.
        std::optional<DockDrop> drop;
        // The window the preview is drawn in (its client pixels). None: nothing is shown (an OS window opens on
        // release).
        std::optional<WindowId> preview_window;
        // The drop's preview in `preview_window`.
        render::Rect preview_rect{};
    };

    // Measured tab widths of a space's registered panels (measure_dock_tabs). Empty: every tab is
    // DockMetrics::tab_width.
    struct DockTabWidths {
        // What the widths were measured with. Any change measures every tab again.
        const void *painter = nullptr;
        const void *sheet = nullptr;
        std::uint64_t sheet_generation = 0;
        float reserve = 0.0f;
        glm::vec2 window{};
        // Panel key -> title it was measured with, closable, and the width.
        struct Tab {
            std::string title;
            bool closable = false;
            float width = 0.0f;
        };
        std::map<std::string, Tab, std::less<>> by_key;
    };

    // The OS window of one DockFloatMode::OsWindow float.
    struct DockFloatWindow {
        WindowId window{};
        // Client rect in screen pixels as last applied or read; a different one read back is a native move or resize.
        glm::ivec2 position{};
        glm::ivec2 size{};
        // The float's stored rect when `position` and `size` were applied or read; a different one is a layout
        // change to apply to the window.
        render::Rect rect{};
        std::string title;
    };

    struct DockSpaceRuntime {
        DockChromeCanvas docked;
        std::map<DockNodeId, DockChromeCanvas> floats;
        // OS windows of the floats, by float id. Only in DockFloatMode::OsWindow; a float without one is virtual.
        std::map<DockNodeId, DockFloatWindow> windows;
        // A window did not open: the space keeps its floats virtual until its mode changes.
        bool windows_failed = false;
        DockFloatMode windows_mode = DockFloatMode::Virtual;
        DockChromeCanvas preview;
        DockGesture gesture;
        // Panel canvases shown by the last layout pass; one that hides loses its popups and focus.
        std::set<ecs::Entity> shown;
        DockTabWidths tabs;
    };

    // world.ctx<DockRuntime>(): every dock space of the world, by the entity that holds its DockSpace.
    struct DockRuntime {
        std::map<ecs::Entity, DockSpaceRuntime> spaces;
        ecs::EventCursor<MouseEvent> mouse;
        ecs::EventCursor<KeyEvent> keys;
        ecs::EventCursor<WindowCloseRequestedEvent> closes;
    };

    // Frame / Input, after run_input: pointer and Escape on chrome, gestures, layout changes, MouseConsumed; the
    // close button of an OS float window docks its panels back.
    void run_dock_input(ecs::World &world, const EngineSystemDeps &deps);
    // Frame / Bind, before run_bind: opens, moves and closes OS float windows; geometry onto panel canvases and chrome
    // canvases; spawns and destroys chrome.
    void run_dock_layout(ecs::World &world, const EngineSystemDeps &deps);

    // Where a float is shown: a window and the float's frame in that window's client pixels.
    struct DockFloatHome {
        WindowId window{};
        render::Rect rect{};
        // In an OS window of its own: no frame or title bar, the float's tree fills `rect`.
        bool os_window = false;
    };

    // Where float `float_id` of `space` with stored rect `frame` is shown. The one place a float's home is decided:
    // the OS window the runtime opened for it (DockFloatMode::OsWindow), its client area at the origin; otherwise
    // the space's window, kept inside the dock area.
    [[nodiscard]] DockFloatHome dock_float_home(const DockSpace &space, const DockSpaceRuntime &runtime,
                                                DockNodeId float_id, render::Rect frame);

    // The OS window of float `float_id`, if it has one.
    [[nodiscard]] std::optional<WindowId> dock_float_window(const DockSpaceRuntime &runtime, DockNodeId float_id);
    // The float whose OS window is `window`, or kNoDockNode.
    [[nodiscard]] DockNodeId dock_float_in_window(const DockSpaceRuntime &runtime, WindowId window);
    // The space's window or one of its float windows.
    [[nodiscard]] bool dock_space_owns_window(const DockSpace &space, const DockSpaceRuntime &runtime,
                                              WindowId window);

    // Metrics of the tree in an OS float window: no frame border, no title bar.
    [[nodiscard]] DockMetrics dock_os_float_metrics(DockMetrics metrics);

    // Opens a window for every float that lacks one, closes those of floats that went, follows native moves and
    // resizes into the layout (bumping `revision`) and applies layout rects and titles to the windows. In
    // DockFloatMode::Virtual, or without window control, closes every window of the space.
    void sync_dock_float_windows(ecs::World &world, ecs::Entity entity, DockSpaceRuntime &runtime,
                                 const EngineSystemDeps &deps);
    // Closes every OS float window of a space (the space went, or its floats are virtual now).
    void close_dock_float_windows(DockSpaceRuntime &runtime, const EngineSystemDeps &deps);
    // Closes every OS float window of every dock space of `world`: Worlds::destroy, before the world goes. Outside
    // draw_all, like the layout pass: destroy_window drops each window's command buffer with it.
    void close_world_dock_float_windows(ecs::World &world, const EngineSystemDeps &deps);
    // The close button of a float's OS window: its panels go back to the docked tree. A float of one stack joins the
    // first docked stack; a float with splits goes to the right edge of the dock area; an empty docked tree takes
    // it whole. Returns true when the layout changed.
    bool dock_float_window_closed(DockSpace &space, DockNodeId float_id);
    // Where the space's window is on screen; the origin when unknown.
    [[nodiscard]] glm::ivec2 dock_space_screen_origin(const DockSpace &space, const EngineSystemDeps &deps);

    // Fills runtime.tabs from the titles of the space's registered panels: the window's layout painter measures each
    // title with the font, size and padding the chrome stylesheet gives `.dock-tab` (a closable tab with
    // `--reserve` set, so its padding clears the close button). Without a painter or a loaded chrome stylesheet
    // (headless, the first frame) the widths are cleared.
    void measure_dock_tabs(ecs::World &world, const DockSpace &space, DockSpaceRuntime &runtime);

    // Room a closable tab keeps on its right for the close button: the button and its gap to the tab's right edge.
    [[nodiscard]] float dock_close_reserve(float tab_height, float close_size) noexcept;

    // The metrics the space is laid out with: the host's, plus the measured tab widths when the host sets no
    // `tab_width_for`. A tab with no measured width is `tab_width`. Refers to `runtime`; use it while that lives.
    [[nodiscard]] DockMetrics dock_space_metrics(const DockSpace &space, const DockSpaceRuntime &runtime);

    // Geometry of the space's own window as shown: dock_space_metrics, virtual floats at their homes. Floats in OS
    // windows are left out.
    [[nodiscard]] DockGeometry dock_space_geometry(const DockSpace &space, const DockSpaceRuntime &runtime);
    // Geometry of the OS window of float `float_id`, in its client pixels: that float alone, its frame the client
    // area, no title bar and no border, and an empty `area` (nothing there is the dock area).
    [[nodiscard]] DockGeometry dock_float_window_geometry(const DockSpace &space, const DockSpaceRuntime &runtime,
                                                          DockNodeId float_id);

    [[nodiscard]] const DockPanel *dock_panel(const DockSpace &space, std::string_view key);
    // The panel's title, or the key when it has none or is not registered.
    [[nodiscard]] std::string dock_panel_title(const DockSpace &space, std::string_view key);

    // UiCanvas orders of a space, from DockSpace::order up (dock_space_order_count of them).
    [[nodiscard]] inline int dock_docked_chrome_order(const DockSpace &space) { return space.order; }
    [[nodiscard]] inline int dock_docked_panel_order(const DockSpace &space) { return space.order + 1; }
    // Chrome of the float at z index `z` (0 is the bottom float).
    [[nodiscard]] int dock_float_chrome_order(const DockSpace &space, std::size_t z);
    [[nodiscard]] int dock_float_panel_order(const DockSpace &space, DockNodeId float_id);
    [[nodiscard]] int dock_preview_order(const DockSpace &space);

} // namespace engine::ui
