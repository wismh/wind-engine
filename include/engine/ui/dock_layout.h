#pragma once

// docs/tech/features/Docking.md

#include <engine/render/commands.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

    // Id of a dock node or of a float. Both come from one counter, so they never collide inside a layout, and an
    // id is never reused by the same layout.
    using DockNodeId = std::uint32_t;

    // No node: the docked root of an empty layout, a split's missing child, or "the dock area" in a DockTarget.
    inline constexpr DockNodeId kNoDockNode = 0;

    // Split ratios stay inside [kDockRatioMin, kDockRatioMax].
    inline constexpr float kDockRatioMin = 0.01f;
    inline constexpr float kDockRatioMax = 0.99f;

    enum class DockAxis {
        // `first` left, `second` right.
        Horizontal,
        // `first` on top, `second` below.
        Vertical,
    };

    // Where a panel goes relative to a node. Center joins a tab stack; an edge splits the node and puts the panel
    // on that side.
    enum class DockZone {
        Center,
        Left,
        Right,
        Top,
        Bottom,
    };

    enum class DockNodeKind {
        Tabs,
        Split,
    };

    // One node of a dock tree. A Tabs node uses `panels` and `active`; a Split node uses `axis`, `ratio`,
    // `first` and `second`. `parent` is kNoDockNode on the root of the docked tree and of each float.
    struct DockNode {
        DockNodeId id = kNoDockNode;
        DockNodeId parent = kNoDockNode;
        DockNodeKind kind = DockNodeKind::Tabs;

        std::vector<std::string> panels;
        std::size_t active = 0;

        DockAxis axis = DockAxis::Horizontal;
        // Share of the split's length (after the splitter) that `first` gets.
        float ratio = 0.5f;
        DockNodeId first = kNoDockNode;
        DockNodeId second = kNoDockNode;

        bool operator==(const DockNode &) const = default;
    };

    // A floating window of the layout: its own subtree and its frame rect in the dock space's window.
    struct DockFloat {
        DockNodeId id = kNoDockNode;
        DockNodeId root = kNoDockNode;
        render::Rect rect{};

        bool operator==(const DockFloat &) const = default;
    };

    // Where to put a panel. `node` is any node of the docked tree or of a float, or kNoDockNode for the dock area
    // itself: an edge zone then splits the docked root, Center fills an empty root or joins a Tabs root.
    struct DockTarget {
        DockNodeId node = kNoDockNode;
        DockZone zone = DockZone::Center;

        bool operator==(const DockTarget &) const = default;
    };

    // Where a panel is: its tab stack, its tab index, and the float that holds the stack (kNoDockNode when docked).
    struct DockPanelPlace {
        DockNodeId stack = kNoDockNode;
        std::size_t index = 0;
        DockNodeId float_id = kNoDockNode;

        bool operator==(const DockPanelPlace &) const = default;
    };

    // The dock model: one docked tree (maybe empty) and floats in z order, last on top. Panels are stable string
    // keys. Every operation leaves the invariants (`valid()`) true and changes nothing when it returns false.
    class DockLayout {
    public:
        DockLayout() = default;

        // A layout from stored parts (deserialization). Nullopt when the parts break an invariant.
        [[nodiscard]] static std::optional<DockLayout> from_parts(std::map<DockNodeId, DockNode> nodes,
                                                                  DockNodeId root, std::vector<DockFloat> floats,
                                                                  DockNodeId next_id);

        [[nodiscard]] DockNodeId root() const { return root_; }
        [[nodiscard]] const std::map<DockNodeId, DockNode> &nodes() const { return nodes_; }
        [[nodiscard]] std::span<const DockFloat> floats() const { return floats_; }
        [[nodiscard]] DockNodeId next_id() const { return next_id_; }

        [[nodiscard]] const DockNode *node(DockNodeId id) const;
        [[nodiscard]] const DockFloat *find_float(DockNodeId float_id) const;
        // The float whose subtree holds `node`, or kNoDockNode for a docked node or an unknown id.
        [[nodiscard]] DockNodeId float_of(DockNodeId node) const;

        [[nodiscard]] bool empty() const { return root_ == kNoDockNode && floats_.empty(); }
        [[nodiscard]] std::optional<DockPanelPlace> find(std::string_view key) const;
        [[nodiscard]] bool contains(std::string_view key) const { return find(key).has_value(); }
        // True when the panel is the active tab of its stack.
        [[nodiscard]] bool is_visible(std::string_view key) const;
        // Every panel: the docked tree depth first, then each float bottom to top.
        [[nodiscard]] std::vector<std::string> panels() const;
        // Tab stacks of one tree depth first, `first` before `second`.
        [[nodiscard]] std::vector<DockNodeId> stacks_under(DockNodeId node) const;

        // Adds a new panel (non-empty, not yet in the layout) at `target` and makes it the active tab.
        bool add(std::string key, DockTarget target);
        bool remove(std::string_view key);
        // Takes the panel from wherever it is (a float too) and puts it at `target`, active. False when that is no
        // change: Center on its own stack, or any zone of a stack that holds only this panel.
        bool move(std::string_view key, DockTarget target);
        // Takes the panel out into a new float on top with frame `rect`. A panel alone in a float only gets the rect
        // and is raised.
        bool float_panel(std::string_view key, render::Rect rect);
        // Docks a whole float at `target`, outside that float. Center appends its tabs to a stack (the float's root
        // must be a stack); an edge inserts its subtree.
        bool dock_float(DockNodeId float_id, DockTarget target);
        bool set_float_rect(DockNodeId float_id, render::Rect rect);
        // Puts the float on top of the z order.
        bool raise_float(DockNodeId float_id);
        bool activate(std::string_view key);
        // Moves a tab inside its stack to `index` (clamped to the last tab). The active panel stays active.
        bool reorder(std::string_view key, std::size_t index);
        // Clamped to [kDockRatioMin, kDockRatioMax]. False for a non-split node or a NaN.
        bool set_ratio(DockNodeId split, float ratio);

        // Panel keys unique and non-empty, no empty tab stack, every split has two children, parents match, every
        // node reachable from exactly one root, no empty float, active index in range, ratios in range.
        [[nodiscard]] bool valid() const;

        bool operator==(const DockLayout &) const = default;

    private:
        DockNodeId root_ = kNoDockNode;
        std::map<DockNodeId, DockNode> nodes_;
        std::vector<DockFloat> floats_;
        DockNodeId next_id_ = 1;

        DockNode *node_mut(DockNodeId id);
        DockNodeId make_stack(std::string key);
        void replace_child(DockNodeId parent, DockNodeId old_child, DockNodeId new_child);
        // Removes the key; returns the split that collapsed (and its survivor) when the stack went away.
        bool take(std::string_view key, DockNodeId &collapsed, DockNodeId &survivor);
        // Puts a detached subtree at an edge of `target`, or into an empty docked root.
        bool insert(DockNodeId subtree, DockTarget target);
        // Center: appends panels to a tab stack (`target` a stack, or the dock area with a Tabs root).
        DockNodeId center_stack(DockTarget target) const;
        void erase_subtree(DockNodeId id);
    };

    // TOML text of the layout: ids, ratios, panel keys, float rects. dock_layout_from_text(dock_layout_to_text(x))
    // == x.
    [[nodiscard]] std::string dock_layout_to_text(const DockLayout &layout);
    // Nullopt on a parse error, a wrong type, an unknown version, or a layout that breaks an invariant.
    [[nodiscard]] std::optional<DockLayout> dock_layout_from_text(std::string_view text);

    // Where reconcile_dock_layout puts a registered panel the layout lacks: next to the panel `beside` (Center joins
    // its stack, an edge splits it), or at `zone` of the dock area when `beside` is not in the layout.
    struct DockSpot {
        std::string beside;
        DockZone zone = DockZone::Center;
    };

    // Drops every panel not in `registered` and adds every registered panel the layout lacks at `spot`, in
    // `registered` order. When the spot cannot take it (the dock area is a split and the zone is Center), the panel
    // joins the first docked stack. Returns true when anything changed.
    bool reconcile_dock_layout(DockLayout &layout, std::span<const std::string> registered, const DockSpot &spot);

} // namespace engine::ui
