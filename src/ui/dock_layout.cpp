#include <engine/ui/dock_layout.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace engine::ui {

namespace {

    [[nodiscard]] DockAxis axis_for(DockZone zone) {
        return zone == DockZone::Left || zone == DockZone::Right ? DockAxis::Horizontal : DockAxis::Vertical;
    }

    [[nodiscard]] bool goes_first(DockZone zone) { return zone == DockZone::Left || zone == DockZone::Top; }

} // namespace

std::optional<DockLayout> DockLayout::from_parts(std::map<DockNodeId, DockNode> nodes, DockNodeId root,
                                                 std::vector<DockFloat> floats, DockNodeId next_id) {
    DockLayout layout;
    layout.nodes_ = std::move(nodes);
    layout.root_ = root;
    layout.floats_ = std::move(floats);
    layout.next_id_ = next_id;
    if (!layout.valid()) {
        return std::nullopt;
    }
    return layout;
}

const DockNode *DockLayout::node(DockNodeId id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

DockNode *DockLayout::node_mut(DockNodeId id) {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const DockFloat *DockLayout::find_float(DockNodeId float_id) const {
    const auto it = std::ranges::find(floats_, float_id, &DockFloat::id);
    return it == floats_.end() ? nullptr : &*it;
}

DockNodeId DockLayout::float_of(DockNodeId id) const {
    const DockNode *current = node(id);
    if (current == nullptr) {
        return kNoDockNode;
    }
    // A parent chain is never longer than the node count; the bound keeps a broken layout from looping.
    for (std::size_t steps = 0; current->parent != kNoDockNode && steps < nodes_.size(); ++steps) {
        current = node(current->parent);
        if (current == nullptr) {
            return kNoDockNode;
        }
    }
    const auto it = std::ranges::find(floats_, current->id, &DockFloat::root);
    return it == floats_.end() ? kNoDockNode : it->id;
}

std::optional<DockPanelPlace> DockLayout::find(std::string_view key) const {
    for (const auto &[id, n]: nodes_) {
        if (n.kind != DockNodeKind::Tabs) {
            continue;
        }
        const auto it = std::ranges::find(n.panels, key);
        if (it != n.panels.end()) {
            return DockPanelPlace{id, static_cast<std::size_t>(it - n.panels.begin()), float_of(id)};
        }
    }
    return std::nullopt;
}

bool DockLayout::is_visible(std::string_view key) const {
    const std::optional<DockPanelPlace> place = find(key);
    return place && node(place->stack)->active == place->index;
}

std::vector<DockNodeId> DockLayout::stacks_under(DockNodeId id) const {
    std::vector<DockNodeId> out;
    std::vector<DockNodeId> pending;
    if (id != kNoDockNode) {
        pending.push_back(id);
    }
    while (!pending.empty() && out.size() <= nodes_.size()) {
        const DockNode *n = node(pending.back());
        pending.pop_back();
        if (n == nullptr) {
            continue;
        }
        if (n->kind == DockNodeKind::Tabs) {
            out.push_back(n->id);
        } else {
            pending.push_back(n->second);
            pending.push_back(n->first);
        }
    }
    return out;
}

std::vector<std::string> DockLayout::panels() const {
    std::vector<std::string> out;
    const auto append = [&](DockNodeId tree) {
        for (const DockNodeId stack: stacks_under(tree)) {
            const DockNode &n = *node(stack);
            out.insert(out.end(), n.panels.begin(), n.panels.end());
        }
    };
    append(root_);
    for (const DockFloat &f: floats_) {
        append(f.root);
    }
    return out;
}

DockNodeId DockLayout::make_stack(std::string key) {
    const DockNodeId id = next_id_++;
    DockNode n;
    n.id = id;
    n.kind = DockNodeKind::Tabs;
    n.panels.push_back(std::move(key));
    nodes_.emplace(id, std::move(n));
    return id;
}

void DockLayout::replace_child(DockNodeId parent, DockNodeId old_child, DockNodeId new_child) {
    if (parent == kNoDockNode) {
        if (root_ == old_child) {
            root_ = new_child;
        } else {
            for (DockFloat &f: floats_) {
                if (f.root == old_child) {
                    f.root = new_child;
                }
            }
        }
    } else {
        DockNode &p = *node_mut(parent);
        (p.first == old_child ? p.first : p.second) = new_child;
    }
    if (DockNode *n = node_mut(new_child)) {
        n->parent = parent;
    }
}

bool DockLayout::take(std::string_view key, DockNodeId &collapsed, DockNodeId &survivor) {
    collapsed = kNoDockNode;
    survivor = kNoDockNode;
    const std::optional<DockPanelPlace> place = find(key);
    if (!place) {
        return false;
    }
    DockNode &stack = *node_mut(place->stack);
    stack.panels.erase(stack.panels.begin() + static_cast<std::ptrdiff_t>(place->index));
    if (!stack.panels.empty()) {
        if (place->index < stack.active || stack.active >= stack.panels.size()) {
            --stack.active;
        }
        return true;
    }
    const DockNodeId stack_id = stack.id;
    const DockNodeId parent = stack.parent;
    nodes_.erase(stack_id);
    if (parent == kNoDockNode) {
        if (root_ == stack_id) {
            root_ = kNoDockNode;
        } else {
            std::erase_if(floats_, [stack_id](const DockFloat &f) { return f.root == stack_id; });
        }
        return true;
    }
    const DockNode &split = *node(parent);
    const DockNodeId other = split.first == stack_id ? split.second : split.first;
    replace_child(split.parent, parent, other);
    nodes_.erase(parent);
    collapsed = parent;
    survivor = other;
    return true;
}

bool DockLayout::insert(DockNodeId subtree, DockTarget target) {
    if (target.node == kNoDockNode && root_ == kNoDockNode) {
        root_ = subtree;
        node_mut(subtree)->parent = kNoDockNode;
        return true;
    }
    if (target.zone == DockZone::Center) {
        return false;
    }
    const DockNodeId at = target.node == kNoDockNode ? root_ : target.node;
    if (node(at) == nullptr) {
        return false;
    }
    const DockNodeId split_id = next_id_++;
    DockNode split;
    split.id = split_id;
    split.kind = DockNodeKind::Split;
    split.axis = axis_for(target.zone);
    split.first = goes_first(target.zone) ? subtree : at;
    split.second = goes_first(target.zone) ? at : subtree;
    const DockNodeId parent = node(at)->parent;
    nodes_.emplace(split_id, std::move(split));
    replace_child(parent, at, split_id);
    node_mut(at)->parent = split_id;
    node_mut(subtree)->parent = split_id;
    return true;
}

DockNodeId DockLayout::center_stack(DockTarget target) const {
    const DockNodeId at = target.node == kNoDockNode ? root_ : target.node;
    const DockNode *n = node(at);
    return n != nullptr && n->kind == DockNodeKind::Tabs ? at : kNoDockNode;
}

void DockLayout::erase_subtree(DockNodeId id) {
    const DockNode *n = node(id);
    if (n == nullptr) {
        return;
    }
    if (n->kind == DockNodeKind::Split) {
        const DockNodeId first = n->first;
        const DockNodeId second = n->second;
        erase_subtree(first);
        erase_subtree(second);
    }
    nodes_.erase(id);
}

bool DockLayout::add(std::string key, DockTarget target) {
    if (key.empty() || contains(key) || (target.node != kNoDockNode && node(target.node) == nullptr)) {
        return false;
    }
    // Center into an empty dock area makes the root stack below; every other Center joins a stack.
    if (target.zone == DockZone::Center && !(target.node == kNoDockNode && root_ == kNoDockNode)) {
        const DockNodeId stack = center_stack(target);
        if (stack == kNoDockNode) {
            return false;
        }
        DockNode &n = *node_mut(stack);
        n.panels.push_back(std::move(key));
        n.active = n.panels.size() - 1;
        return true;
    }
    DockLayout next = *this;
    const DockNodeId stack = next.make_stack(std::move(key));
    if (!next.insert(stack, target)) {
        return false;
    }
    *this = std::move(next);
    return true;
}

bool DockLayout::remove(std::string_view key) {
    DockNodeId collapsed = kNoDockNode;
    DockNodeId survivor = kNoDockNode;
    return take(key, collapsed, survivor);
}

bool DockLayout::move(std::string_view key, DockTarget target) {
    const std::optional<DockPanelPlace> place = find(key);
    if (!place || (target.node != kNoDockNode && node(target.node) == nullptr)) {
        return false;
    }
    const bool alone = node(place->stack)->panels.size() == 1;
    const DockNodeId at = target.node == kNoDockNode ? root_ : target.node;
    if (at == place->stack && (alone || target.zone == DockZone::Center)) {
        return false;
    }
    DockLayout next = *this;
    std::string owned(key);
    DockNodeId collapsed = kNoDockNode;
    DockNodeId survivor = kNoDockNode;
    next.take(key, collapsed, survivor);
    if (target.node != kNoDockNode && target.node == collapsed) {
        target.node = survivor;
    }
    if (!next.add(std::move(owned), target)) {
        return false;
    }
    *this = std::move(next);
    return true;
}

bool DockLayout::float_panel(std::string_view key, render::Rect rect) {
    const std::optional<DockPanelPlace> place = find(key);
    if (!place) {
        return false;
    }
    if (place->float_id != kNoDockNode && find_float(place->float_id)->root == place->stack &&
        node(place->stack)->panels.size() == 1) {
        set_float_rect(place->float_id, rect);
        raise_float(place->float_id);
        return true;
    }
    std::string owned(key);
    DockNodeId collapsed = kNoDockNode;
    DockNodeId survivor = kNoDockNode;
    take(key, collapsed, survivor);
    const DockNodeId stack = make_stack(std::move(owned));
    floats_.push_back(DockFloat{next_id_++, stack, rect});
    return true;
}

bool DockLayout::dock_float(DockNodeId float_id, DockTarget target) {
    const DockFloat *f = find_float(float_id);
    if (f == nullptr || (target.node != kNoDockNode && node(target.node) == nullptr) ||
        (target.node != kNoDockNode && float_of(target.node) == float_id)) {
        return false;
    }
    DockLayout next = *this;
    const DockNodeId subtree = f->root;
    std::erase_if(next.floats_, [float_id](const DockFloat &x) { return x.id == float_id; });
    if (target.zone == DockZone::Center && !(target.node == kNoDockNode && next.root_ == kNoDockNode)) {
        const DockNodeId stack = next.center_stack(target);
        const DockNode &source = *next.node(subtree);
        if (stack == kNoDockNode || source.kind != DockNodeKind::Tabs) {
            return false;
        }
        DockNode &dest = *next.node_mut(stack);
        dest.active = dest.panels.size() + source.active;
        dest.panels.insert(dest.panels.end(), source.panels.begin(), source.panels.end());
        next.nodes_.erase(subtree);
    } else if (!next.insert(subtree, target)) {
        return false;
    }
    *this = std::move(next);
    return true;
}

bool DockLayout::set_float_rect(DockNodeId float_id, render::Rect rect) {
    const auto it = std::ranges::find(floats_, float_id, &DockFloat::id);
    if (it == floats_.end()) {
        return false;
    }
    it->rect = rect;
    return true;
}

bool DockLayout::raise_float(DockNodeId float_id) {
    const auto it = std::ranges::find(floats_, float_id, &DockFloat::id);
    if (it == floats_.end()) {
        return false;
    }
    std::rotate(it, it + 1, floats_.end());
    return true;
}

bool DockLayout::activate(std::string_view key) {
    const std::optional<DockPanelPlace> place = find(key);
    if (!place) {
        return false;
    }
    node_mut(place->stack)->active = place->index;
    return true;
}

bool DockLayout::reorder(std::string_view key, std::size_t index) {
    const std::optional<DockPanelPlace> place = find(key);
    if (!place) {
        return false;
    }
    DockNode &n = *node_mut(place->stack);
    const std::string active = n.panels[n.active];
    std::string moved = std::move(n.panels[place->index]);
    n.panels.erase(n.panels.begin() + static_cast<std::ptrdiff_t>(place->index));
    index = std::min(index, n.panels.size());
    n.panels.insert(n.panels.begin() + static_cast<std::ptrdiff_t>(index), std::move(moved));
    n.active = static_cast<std::size_t>(std::ranges::find(n.panels, active) - n.panels.begin());
    return true;
}

bool DockLayout::set_ratio(DockNodeId split, float ratio) {
    DockNode *n = node_mut(split);
    if (n == nullptr || n->kind != DockNodeKind::Split || std::isnan(ratio)) {
        return false;
    }
    n->ratio = std::clamp(ratio, kDockRatioMin, kDockRatioMax);
    return true;
}

bool DockLayout::valid() const {
    std::unordered_set<DockNodeId> seen;
    std::unordered_set<std::string_view> keys;
    for (const auto &[id, n]: nodes_) {
        if (id == kNoDockNode || id != n.id || id >= next_id_) {
            return false;
        }
    }
    // Walks one tree; false on a cycle, a shared node, a bad parent link, or a node that breaks its own rules.
    const auto walk = [&](DockNodeId tree) {
        std::vector<std::pair<DockNodeId, DockNodeId>> pending{{tree, kNoDockNode}};
        while (!pending.empty()) {
            const auto [id, parent] = pending.back();
            pending.pop_back();
            const DockNode *n = node(id);
            if (n == nullptr || n->parent != parent || !seen.insert(id).second) {
                return false;
            }
            if (n->kind == DockNodeKind::Tabs) {
                if (n->panels.empty() || n->active >= n->panels.size()) {
                    return false;
                }
                for (const std::string &key: n->panels) {
                    if (key.empty() || !keys.insert(key).second) {
                        return false;
                    }
                }
            } else {
                if (std::isnan(n->ratio) || n->ratio < kDockRatioMin || n->ratio > kDockRatioMax ||
                    n->first == kNoDockNode || n->second == kNoDockNode) {
                    return false;
                }
                pending.emplace_back(n->first, id);
                pending.emplace_back(n->second, id);
            }
        }
        return true;
    };
    if (root_ != kNoDockNode && !walk(root_)) {
        return false;
    }
    std::unordered_set<DockNodeId> float_ids;
    for (const DockFloat &f: floats_) {
        if (f.id == kNoDockNode || f.id >= next_id_ || nodes_.contains(f.id) || !float_ids.insert(f.id).second ||
            f.root == kNoDockNode || !walk(f.root)) {
            return false;
        }
    }
    return seen.size() == nodes_.size();
}

} // namespace engine::ui
