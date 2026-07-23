#pragma once

// docs/tech/features/Docking.md#geometry

#include <engine/render/commands.h>
#include <engine/ui/dock_layout.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

    // Sizes the host draws its chrome with, in the dock space's pixels.
    struct DockMetrics {
        float tab_strip_height = 24.0f;
        float splitter_thickness = 4.0f;
        float title_bar_height = 22.0f;
        // Float frame border; also the width of a float's resize band.
        float frame_border = 4.0f;
        // Smallest width and height a split gives either side, and a float's content when resized.
        float min_panel_size = 48.0f;
        // Tab width when `tab_width_for` is empty. Tabs that do not fit the strip shrink in proportion.
        float tab_width = 120.0f;
        // Host-measured tab width (title text plus padding). No font access here.
        std::function<float(std::string_view key)> tab_width_for;
        // Edge band of a hovered stack, as a share of its width or height.
        float drop_edge_share = 0.25f;
        // Edge band of the whole dock area that docks to the root, in pixels.
        float root_edge_band = 24.0f;
        // Frame size a drop outside every dock rect floats a panel with.
        glm::vec2 float_size{320.0f, 240.0f};
    };

    struct DockPanelRect {
        std::string key;
        DockNodeId stack = kNoDockNode;
        DockNodeId float_id = kNoDockNode;
        // The stack's content rect. An inactive tab has it too, with `shown` false.
        render::Rect content{};
        bool shown = false;
    };

    struct DockTabRect {
        std::string key;
        render::Rect rect{};
        bool active = false;
    };

    struct DockStackRect {
        DockNodeId id = kNoDockNode;
        DockNodeId float_id = kNoDockNode;
        // Tab strip plus content.
        render::Rect rect{};
        render::Rect strip{};
        render::Rect content{};
        std::vector<DockTabRect> tabs;
    };

    struct DockSplitterRect {
        DockNodeId split = kNoDockNode;
        DockNodeId float_id = kNoDockNode;
        DockAxis axis = DockAxis::Horizontal;
        // The bar between the two children.
        render::Rect grab{};
        // The whole split, which dock_split_ratio_at measures against.
        render::Rect area{};
    };

    struct DockFloatRect {
        DockNodeId id = kNoDockNode;
        // Outer rect: the float's stored rect.
        render::Rect frame{};
        render::Rect title{};
        render::Rect content{};
    };

    // Everything a host needs to place panel canvases and draw chrome. Stacks and splitters of the docked tree
    // come first, then each float's in z order. `floats` is bottom to top.
    struct DockGeometry {
        render::Rect area{};
        std::vector<DockPanelRect> panels;
        std::vector<DockStackRect> stacks;
        std::vector<DockSplitterRect> splitters;
        std::vector<DockFloatRect> floats;

        [[nodiscard]] const DockPanelRect *panel(std::string_view key) const;
        [[nodiscard]] const DockStackRect *stack(DockNodeId id) const;
        [[nodiscard]] const DockSplitterRect *splitter(DockNodeId split) const;
        [[nodiscard]] const DockFloatRect *floating(DockNodeId float_id) const;
    };

    // The docked tree fills `area`; floats sit at their stored rects. A split gives each side at least
    // `min_panel_size` when it has room for both, otherwise it keeps its ratio.
    [[nodiscard]] DockGeometry compute_dock_geometry(const DockLayout &layout, render::Rect area,
                                                     const DockMetrics &metrics);

    // Child rects of a split laid out in `area`: `first`, then `second`, with the splitter between them.
    struct DockSplitRects {
        render::Rect first{};
        render::Rect grab{};
        render::Rect second{};
    };

    [[nodiscard]] DockSplitRects dock_split_rects(render::Rect area, DockAxis axis, float ratio,
                                                  const DockMetrics &metrics);

    // The ratio that puts the splitter's middle under `point`, keeping both sides at least `min_panel_size`
    // when there is room for both (0.5 when there is not), inside [kDockRatioMin, kDockRatioMax].
    [[nodiscard]] float dock_split_ratio_at(const DockSplitterRect &splitter, glm::vec2 point,
                                            const DockMetrics &metrics);

    // Which edges a float resize drags. A corner sets two.
    struct DockResizeEdges {
        bool left = false;
        bool right = false;
        bool top = false;
        bool bottom = false;

        bool operator==(const DockResizeEdges &) const = default;
    };

    // `start` resized by dragging `edges` by `delta`. The content keeps `min_panel_size` each way; a dragged left or
    // top edge stops instead of pushing the opposite edge.
    [[nodiscard]] render::Rect dock_float_resized(render::Rect start, DockResizeEdges edges, glm::vec2 delta,
                                                  const DockMetrics &metrics);

    // `rect` moved (not resized) to lie inside `area`. A rect wider or taller than the area aligns to its left or top
    // edge.
    [[nodiscard]] render::Rect dock_float_clamped(render::Rect rect, render::Rect area);

    enum class DockChromeKind {
        Tab,
        Splitter,
        FloatTitle,
        FloatEdge,
    };

    // A piece of chrome under the pointer. `node` is the tab's stack, the split, or the float.
    struct DockChromeHit {
        DockChromeKind kind = DockChromeKind::Tab;
        DockNodeId node = kNoDockNode;
        // The tab's panel.
        std::string key;
        // Set for FloatEdge.
        DockResizeEdges edges;

        bool operator==(const DockChromeHit &) const = default;
    };

    // Topmost float first. A float covers what is under it: a point on a float's content hits nothing.
    [[nodiscard]] std::optional<DockChromeHit> dock_chrome_at(const DockGeometry &geometry, const DockMetrics &metrics,
                                                              glm::vec2 point);

    // The topmost float whose frame holds `point`, or kNoDockNode. A click there raises it.
    [[nodiscard]] DockNodeId dock_float_at(const DockGeometry &geometry, glm::vec2 point);

    enum class DockDropKind {
        // Apply with DockLayout::move (a panel) or DockLayout::dock_float (a float).
        Dock,
        // Apply with DockLayout::float_panel(key, preview).
        Float,
    };

    struct DockDrop {
        DockDropKind kind = DockDropKind::Dock;
        DockTarget target;
        // Where the panel would go: half the hovered stack or dock area for an edge, the whole stack for Center,
        // the new frame for Float.
        render::Rect preview{};

        bool operator==(const DockDrop &) const = default;
    };

    // Drop of panel `key` dragged by its tab with the pointer at `point`. Floats are tested top first. Inside the
    // dock area a tab strip joins its stack (Center), then a root edge band wins over the stack under it, then the
    // stack's centre and edge bands apply. Outside every dock rect the panel floats, its frame
    // placed so the pointer sits at `grab` inside it. Nullopt where a drop changes nothing (its own single-panel
    // stack, Center on its own stack) or lands on no stack (a splitter, a float title). A float that holds only
    // `key` is skipped, since it moves with the pointer.
    [[nodiscard]] std::optional<DockDrop> dock_drop_for_panel(const DockLayout &layout, const DockGeometry &geometry,
                                                              const DockMetrics &metrics, glm::vec2 point,
                                                              std::string_view key, glm::vec2 grab);

    // Drop of the whole float `float_id`, dragged by its title bar. That float is skipped. Only dock targets: outside
    // every dock rect it stays a float, so the result is nullopt. Center needs the float's root to be a stack.
    [[nodiscard]] std::optional<DockDrop> dock_drop_for_float(const DockLayout &layout, const DockGeometry &geometry,
                                                              const DockMetrics &metrics, glm::vec2 point,
                                                              DockNodeId float_id);

} // namespace engine::ui
