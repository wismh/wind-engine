#include <engine/ui/dock_layout.h>

#include <algorithm>

namespace engine::ui {

bool reconcile_dock_layout(DockLayout &layout, std::span<const std::string> registered, const DockSpot &spot) {
    bool changed = false;
    for (const std::string &key: layout.panels()) {
        if (std::ranges::find(registered, key) == registered.end()) {
            changed |= layout.remove(key);
        }
    }
    for (const std::string &key: registered) {
        if (key.empty() || layout.contains(key)) {
            continue;
        }
        bool placed = false;
        if (const std::optional<DockPanelPlace> beside = layout.find(spot.beside)) {
            placed = layout.add(key, {beside->stack, spot.zone});
        } else {
            placed = layout.add(key, {kNoDockNode, spot.zone});
        }
        if (!placed) {
            const std::vector<DockNodeId> stacks = layout.stacks_under(layout.root());
            placed = !stacks.empty() && layout.add(key, {stacks.front(), DockZone::Center});
        }
        changed |= placed;
    }
    return changed;
}

} // namespace engine::ui
