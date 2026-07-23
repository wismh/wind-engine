#include <engine/ui/dock_geometry.h>

#include <algorithm>
#include <array>
#include <utility>

namespace engine::ui {

namespace {

    [[nodiscard]] bool inside(const render::Rect &r, glm::vec2 p) {
        return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
    }

    // The nearest edge whose distance from `p` is under its band, else Center. Ties go Left, Right, Top, Bottom.
    [[nodiscard]] DockZone zone_in(const render::Rect &r, glm::vec2 p, float band_x, float band_y) {
        const std::array<std::pair<DockZone, float>, 4> edges{{
                {DockZone::Left, band_x > 0.0f ? (p.x - r.x) / band_x : 2.0f},
                {DockZone::Right, band_x > 0.0f ? (r.x + r.w - p.x) / band_x : 2.0f},
                {DockZone::Top, band_y > 0.0f ? (p.y - r.y) / band_y : 2.0f},
                {DockZone::Bottom, band_y > 0.0f ? (r.y + r.h - p.y) / band_y : 2.0f},
        }};
        const auto nearest = std::ranges::min_element(edges, {}, &std::pair<DockZone, float>::second);
        return nearest->second < 1.0f ? nearest->first : DockZone::Center;
    }

    // Half of `r` on the zone's side, the way a 0.5 split lays it out. Center is all of `r`.
    [[nodiscard]] render::Rect preview_for(const render::Rect &r, DockZone zone, const DockMetrics &metrics) {
        if (zone == DockZone::Center) {
            return r;
        }
        const DockAxis axis = zone == DockZone::Left || zone == DockZone::Right ? DockAxis::Horizontal
                                                                                : DockAxis::Vertical;
        const DockSplitRects parts = dock_split_rects(r, axis, 0.5f, metrics);
        return zone == DockZone::Left || zone == DockZone::Top ? parts.first : parts.second;
    }

    // What lies under a drag: blocked (a float frame with no stack under the point), outside the dock area, a stack
    // and its zone, the dock area's root edge band, or nothing (a splitter).
    struct Under {
        bool blocked = false;
        bool outside = false;
        const DockStackRect *stack = nullptr;
        std::optional<DockZone> root_edge;
        DockZone zone = DockZone::Center;
    };

    [[nodiscard]] Under under(const DockLayout &layout, const DockGeometry &geometry, const DockMetrics &metrics,
                              glm::vec2 p, DockNodeId skip_float) {
        Under u;
        const auto stack_at = [&](DockNodeId float_id) {
            for (const DockStackRect &s: geometry.stacks) {
                if (s.float_id == float_id && inside(s.rect, p)) {
                    u.stack = &s;
                    u.zone = inside(s.strip, p) ? DockZone::Center
                                                : zone_in(s.rect, p, s.rect.w * metrics.drop_edge_share,
                                                          s.rect.h * metrics.drop_edge_share);
                    return;
                }
            }
        };
        for (auto it = geometry.floats.rbegin(); it != geometry.floats.rend(); ++it) {
            if (it->id == skip_float || !inside(it->frame, p)) {
                continue;
            }
            stack_at(it->id);
            u.blocked = u.stack == nullptr;
            return u;
        }
        if (!inside(geometry.area, p)) {
            u.outside = true;
            return u;
        }
        if (layout.root() == kNoDockNode) {
            u.root_edge = DockZone::Center;
            return u;
        }
        // A tab strip joins its stack even inside the root band, so the top row of tabs stays a drop target.
        for (const DockStackRect &s: geometry.stacks) {
            if (s.float_id == kNoDockNode && inside(s.strip, p)) {
                u.stack = &s;
                return u;
            }
        }
        const DockZone edge = zone_in(geometry.area, p, metrics.root_edge_band, metrics.root_edge_band);
        if (edge != DockZone::Center) {
            u.root_edge = edge;
            return u;
        }
        stack_at(kNoDockNode);
        return u;
    }

} // namespace

std::optional<DockChromeHit> dock_chrome_at(const DockGeometry &geometry, const DockMetrics &metrics,
                                            glm::vec2 point) {
    const auto in_tree = [&](DockNodeId float_id) -> std::optional<DockChromeHit> {
        for (const DockSplitterRect &s: geometry.splitters) {
            if (s.float_id == float_id && inside(s.grab, point)) {
                return DockChromeHit{DockChromeKind::Splitter, s.split, {}, {}};
            }
        }
        for (const DockStackRect &s: geometry.stacks) {
            if (s.float_id != float_id || !inside(s.strip, point)) {
                continue;
            }
            for (const DockTabRect &tab: s.tabs) {
                if (inside(tab.rect, point)) {
                    return DockChromeHit{DockChromeKind::Tab, s.id, tab.key, {}};
                }
            }
        }
        return std::nullopt;
    };
    for (auto it = geometry.floats.rbegin(); it != geometry.floats.rend(); ++it) {
        const render::Rect &f = it->frame;
        if (!inside(f, point)) {
            continue;
        }
        if (std::optional<DockChromeHit> hit = in_tree(it->id)) {
            return hit;
        }
        if (inside(it->title, point)) {
            return DockChromeHit{DockChromeKind::FloatTitle, it->id, {}, {}};
        }
        const float b = metrics.frame_border;
        const DockResizeEdges edges{point.x < f.x + b, point.x >= f.x + f.w - b, point.y < f.y + b,
                                    point.y >= f.y + f.h - b};
        if (edges != DockResizeEdges{}) {
            return DockChromeHit{DockChromeKind::FloatEdge, it->id, {}, edges};
        }
        return std::nullopt;
    }
    if (!inside(geometry.area, point)) {
        return std::nullopt;
    }
    return in_tree(kNoDockNode);
}

DockNodeId dock_float_at(const DockGeometry &geometry, glm::vec2 point) {
    for (auto it = geometry.floats.rbegin(); it != geometry.floats.rend(); ++it) {
        if (inside(it->frame, point)) {
            return it->id;
        }
    }
    return kNoDockNode;
}

std::optional<DockDrop> dock_drop_for_panel(const DockLayout &layout, const DockGeometry &geometry,
                                            const DockMetrics &metrics, glm::vec2 point, std::string_view key,
                                            glm::vec2 grab) {
    const std::optional<DockPanelPlace> place = layout.find(key);
    if (!place) {
        return std::nullopt;
    }
    const bool alone = layout.node(place->stack)->panels.size() == 1;
    DockNodeId skip = kNoDockNode;
    if (alone && place->float_id != kNoDockNode && layout.find_float(place->float_id)->root == place->stack) {
        skip = place->float_id;
    }
    const Under u = under(layout, geometry, metrics, point, skip);
    if (u.outside) {
        return DockDrop{DockDropKind::Float,
                        {},
                        {point.x - grab.x, point.y - grab.y, metrics.float_size.x, metrics.float_size.y}};
    }
    if (u.root_edge) {
        if (layout.root() == place->stack && alone) {
            return std::nullopt;
        }
        return DockDrop{DockDropKind::Dock, {kNoDockNode, *u.root_edge},
                        preview_for(geometry.area, *u.root_edge, metrics)};
    }
    if (u.stack == nullptr) {
        return std::nullopt;
    }
    if (u.stack->id == place->stack && (alone || u.zone == DockZone::Center)) {
        return std::nullopt;
    }
    return DockDrop{DockDropKind::Dock, {u.stack->id, u.zone}, preview_for(u.stack->rect, u.zone, metrics)};
}

std::optional<DockDrop> dock_drop_for_float(const DockLayout &layout, const DockGeometry &geometry,
                                            const DockMetrics &metrics, glm::vec2 point, DockNodeId float_id) {
    const DockFloat *source = layout.find_float(float_id);
    if (source == nullptr) {
        return std::nullopt;
    }
    const Under u = under(layout, geometry, metrics, point, float_id);
    if (u.root_edge) {
        return DockDrop{DockDropKind::Dock, {kNoDockNode, *u.root_edge},
                        preview_for(geometry.area, *u.root_edge, metrics)};
    }
    if (u.stack == nullptr) {
        return std::nullopt;
    }
    if (u.zone == DockZone::Center && layout.node(source->root)->kind != DockNodeKind::Tabs) {
        return std::nullopt;
    }
    return DockDrop{DockDropKind::Dock, {u.stack->id, u.zone}, preview_for(u.stack->rect, u.zone, metrics)};
}

} // namespace engine::ui
