#include <engine/ui/dock_geometry.h>

#include <algorithm>
#include <utility>

namespace engine::ui {

namespace {

    struct Builder {
        const DockLayout &layout;
        const DockMetrics &metrics;
        DockGeometry &out;

        void stack(const DockNode &n, render::Rect rect, DockNodeId float_id) {
            const float strip_h = std::clamp(metrics.tab_strip_height, 0.0f, std::max(rect.h, 0.0f));
            DockStackRect s;
            s.id = n.id;
            s.float_id = float_id;
            s.rect = rect;
            s.strip = {rect.x, rect.y, rect.w, strip_h};
            s.content = {rect.x, rect.y + strip_h, rect.w, std::max(rect.h - strip_h, 0.0f)};

            std::vector<float> widths;
            float total = 0.0f;
            for (const std::string &key: n.panels) {
                const float w = std::max(metrics.tab_width_for ? metrics.tab_width_for(key) : metrics.tab_width, 0.0f);
                widths.push_back(w);
                total += w;
            }
            const float scale = total > s.strip.w && total > 0.0f ? std::max(s.strip.w, 0.0f) / total : 1.0f;
            float x = s.strip.x;
            for (std::size_t i = 0; i < n.panels.size(); ++i) {
                const float w = widths[i] * scale;
                s.tabs.push_back(DockTabRect{n.panels[i], {x, s.strip.y, w, strip_h}, i == n.active});
                x += w;
                out.panels.push_back(DockPanelRect{n.panels[i], n.id, float_id, s.content, i == n.active});
            }
            out.stacks.push_back(std::move(s));
        }

        void walk(DockNodeId id, render::Rect rect, DockNodeId float_id) {
            const DockNode *n = layout.node(id);
            if (n == nullptr) {
                return;
            }
            if (n->kind == DockNodeKind::Tabs) {
                stack(*n, rect, float_id);
                return;
            }
            const DockSplitRects parts = dock_split_rects(rect, n->axis, n->ratio, metrics);
            out.splitters.push_back(DockSplitterRect{n->id, float_id, n->axis, parts.grab, rect});
            walk(n->first, parts.first, float_id);
            walk(n->second, parts.second, float_id);
        }
    };

    template<typename T, typename Id>
    [[nodiscard]] const T *find_by(const std::vector<T> &items, Id T::*member, Id value) {
        const auto it = std::ranges::find(items, value, member);
        return it == items.end() ? nullptr : &*it;
    }

} // namespace

const DockPanelRect *DockGeometry::panel(std::string_view key) const {
    const auto it = std::ranges::find_if(panels, [key](const DockPanelRect &p) { return p.key == key; });
    return it == panels.end() ? nullptr : &*it;
}

const DockStackRect *DockGeometry::stack(DockNodeId id) const { return find_by(stacks, &DockStackRect::id, id); }

const DockSplitterRect *DockGeometry::splitter(DockNodeId split) const {
    return find_by(splitters, &DockSplitterRect::split, split);
}

const DockFloatRect *DockGeometry::floating(DockNodeId float_id) const {
    return find_by(floats, &DockFloatRect::id, float_id);
}

DockSplitRects dock_split_rects(render::Rect area, DockAxis axis, float ratio, const DockMetrics &metrics) {
    const bool horizontal = axis == DockAxis::Horizontal;
    const float length = std::max(horizontal ? area.w : area.h, 0.0f);
    const float bar = std::clamp(metrics.splitter_thickness, 0.0f, length);
    const float room = length - bar;
    float first = room * ratio;
    if (room >= 2.0f * metrics.min_panel_size) {
        first = std::clamp(first, metrics.min_panel_size, room - metrics.min_panel_size);
    }
    const float second = room - first;
    DockSplitRects out;
    if (horizontal) {
        out.first = {area.x, area.y, first, area.h};
        out.grab = {area.x + first, area.y, bar, area.h};
        out.second = {area.x + first + bar, area.y, second, area.h};
    } else {
        out.first = {area.x, area.y, area.w, first};
        out.grab = {area.x, area.y + first, area.w, bar};
        out.second = {area.x, area.y + first + bar, area.w, second};
    }
    return out;
}

DockGeometry compute_dock_geometry(const DockLayout &layout, render::Rect area, const DockMetrics &metrics) {
    DockGeometry out;
    out.area = area;
    Builder builder{layout, metrics, out};
    builder.walk(layout.root(), area, kNoDockNode);
    for (const DockFloat &f: layout.floats()) {
        const float b = std::max(metrics.frame_border, 0.0f);
        const float inner_w = std::max(f.rect.w - 2.0f * b, 0.0f);
        const float inner_h = std::max(f.rect.h - 2.0f * b, 0.0f);
        const float title_h = std::clamp(metrics.title_bar_height, 0.0f, inner_h);
        DockFloatRect r;
        r.id = f.id;
        r.frame = f.rect;
        r.title = {f.rect.x + b, f.rect.y + b, inner_w, title_h};
        r.content = {f.rect.x + b, f.rect.y + b + title_h, inner_w, inner_h - title_h};
        out.floats.push_back(r);
        builder.walk(f.root, r.content, f.id);
    }
    return out;
}

float dock_split_ratio_at(const DockSplitterRect &splitter, glm::vec2 point, const DockMetrics &metrics) {
    const bool horizontal = splitter.axis == DockAxis::Horizontal;
    const float length = std::max(horizontal ? splitter.area.w : splitter.area.h, 0.0f);
    const float bar = std::clamp(metrics.splitter_thickness, 0.0f, length);
    const float room = length - bar;
    if (room <= 0.0f || room < 2.0f * metrics.min_panel_size) {
        return 0.5f;
    }
    const float start = horizontal ? splitter.area.x : splitter.area.y;
    const float at = horizontal ? point.x : point.y;
    const float first = std::clamp(at - start - bar * 0.5f, metrics.min_panel_size, room - metrics.min_panel_size);
    return std::clamp(first / room, kDockRatioMin, kDockRatioMax);
}

render::Rect dock_float_resized(render::Rect start, DockResizeEdges edges, glm::vec2 delta,
                                const DockMetrics &metrics) {
    const float b = std::max(metrics.frame_border, 0.0f);
    const float min_w = metrics.min_panel_size + 2.0f * b;
    const float min_h = metrics.min_panel_size + 2.0f * b + metrics.title_bar_height;
    render::Rect r = start;
    if (edges.right) {
        r.w = std::max(start.w + delta.x, min_w);
    } else if (edges.left) {
        r.w = std::max(start.w - delta.x, min_w);
        r.x = start.x + start.w - r.w;
    }
    if (edges.bottom) {
        r.h = std::max(start.h + delta.y, min_h);
    } else if (edges.top) {
        r.h = std::max(start.h - delta.y, min_h);
        r.y = start.y + start.h - r.h;
    }
    return r;
}

render::Rect dock_float_clamped(render::Rect rect, render::Rect area) {
    rect.x = rect.w >= area.w ? area.x : std::clamp(rect.x, area.x, area.x + area.w - rect.w);
    rect.y = rect.h >= area.h ? area.y : std::clamp(rect.y, area.y, area.y + area.h - rect.h);
    return rect;
}

} // namespace engine::ui
