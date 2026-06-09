#include "popup.h"

#include <algorithm>

namespace engine::ui {
    namespace {

        BoxInsets resolve_margin(const Element &element, glm::vec2 parent_content) {
            const float font = resolve_font_size(element.font_size, parent_content.x);
            return BoxInsets{
                    resolve_length(element.margin.top, parent_content.y, font),
                    resolve_length(element.margin.right, parent_content.x, font),
                    resolve_length(element.margin.bottom, parent_content.y, font),
                    resolve_length(element.margin.left, parent_content.x, font),
            };
        }

        bool walked(const Element &element) {
            return element.kind != ElementKind::ItemTemplate && element.visible && !element.display_none;
        }

        // Children a walk visits, in paint order. An ItemsControl paints its generated rows, everything
        // else its children.
        std::vector<Element *> painted_children(Element &element) {
            return child_stacking_order(element.kind == ElementKind::ItemsControl ? element.generated_items
                                                                                    : element.children);
        }

        void place_in(Element &element, const SpaceMap &map, glm::vec2 parent_content, const render::Rect &bounds) {
            if (!walked(element)) {
                return;
            }
            const SpaceMap inner = child_map_of(element, map);
            const glm::vec2 basis = content_size_of(element, parent_content);
            for (Element *child: painted_children(element)) {
                if (child->kind != ElementKind::Popup) {
                    place_in(*child, inner, basis, bounds);
                    continue;
                }
                if (!is_open_popup(*child)) {
                    continue;
                }
                const render::Rect shown =
                        place_popup_rect(map.apply(element.layout_rect), {child->layout_rect.w, child->layout_rect.h},
                                         resolve_margin(*child, basis), child->placement, bounds);
                child->popup_offset = {shown.x - child->layout_rect.x, shown.y - child->layout_rect.y};
                place_in(*child, SpaceMap{1.0f, child->popup_offset}, basis, bounds);
            }
        }

        void collect_in(Element &element, std::vector<const Element *> &ancestors, glm::vec2 parent_content,
                        std::vector<OpenPopup> &out) {
            if (!walked(element)) {
                return;
            }
            const glm::vec2 basis = content_size_of(element, parent_content);
            ancestors.push_back(&element);
            for (Element *child: painted_children(element)) {
                if (child->kind != ElementKind::Popup) {
                    collect_in(*child, ancestors, basis, out);
                    continue;
                }
                if (!is_open_popup(*child) || !child->visible) {
                    continue;
                }
                out.push_back(OpenPopup{child, ancestors, basis});
                collect_in(*child, ancestors, basis, out);
            }
            ancestors.pop_back();
        }

        bool any_open_in(const Element &element) {
            if (element.kind == ElementKind::ItemTemplate || element.display_none) {
                return false;
            }
            if (is_open_popup(element)) {
                return true;
            }
            const std::vector<Element> &children =
                    element.kind == ElementKind::ItemsControl ? element.generated_items : element.children;
            return std::any_of(children.begin(), children.end(), [](const Element &child) {
                return (child.kind != ElementKind::Popup || child.open) && any_open_in(child);
            });
        }

        // Start of the span [low, high) a box of `size` takes when it lines up with the start or end of
        // [anchor_low, anchor_low + anchor_size).
        float align_on(bool end, float anchor_low, float anchor_size, float size) {
            return end ? anchor_low + anchor_size - size : anchor_low;
        }

        // Main-axis position: after the anchor (below / right) unless only the space before it is roomier.
        float side_on(bool before, float anchor_low, float anchor_high, float gap_after, float gap_before, float size,
                      float bound_low, float bound_high) {
            const float after = anchor_high + gap_after;
            const float ahead = anchor_low - gap_before - size;
            const float room_after = bound_high - after;
            const float room_before = (anchor_low - gap_before) - bound_low;
            if (before) {
                return size > room_before && room_after > room_before ? after : ahead;
            }
            return size > room_after && room_before > room_after ? ahead : after;
        }

        float clamp_into(float value, float size, float bound_low, float bound_size) {
            return std::clamp(value, bound_low, std::max(bound_low, bound_low + bound_size - size));
        }

    } // namespace

    render::Rect place_popup_rect(const render::Rect &anchor, glm::vec2 size, const BoxInsets &margin,
                                  PopupPlacement placement, const render::Rect &bounds) {
        const bool vertical = placement == PopupPlacement::BottomStart || placement == PopupPlacement::BottomEnd ||
                              placement == PopupPlacement::TopStart || placement == PopupPlacement::TopEnd;
        const bool before = placement == PopupPlacement::TopStart || placement == PopupPlacement::TopEnd ||
                            placement == PopupPlacement::LeftStart || placement == PopupPlacement::LeftEnd;
        const bool end = placement == PopupPlacement::BottomEnd || placement == PopupPlacement::TopEnd ||
                         placement == PopupPlacement::RightEnd || placement == PopupPlacement::LeftEnd;
        float x = 0.0f;
        float y = 0.0f;
        if (vertical) {
            y = side_on(before, anchor.y, anchor.y + anchor.h, margin.top, margin.bottom, size.y, bounds.y,
                        bounds.y + bounds.h);
            x = align_on(end, anchor.x, anchor.w, size.x);
        } else {
            x = side_on(before, anchor.x, anchor.x + anchor.w, margin.left, margin.right, size.x, bounds.x,
                        bounds.x + bounds.w);
            y = align_on(end, anchor.y, anchor.h, size.y);
        }
        return render::Rect{
                clamp_into(x, size.x, bounds.x, bounds.w),
                clamp_into(y, size.y, bounds.y, bounds.h),
                size.x,
                size.y,
        };
    }

    render::Rect popup_bounds(const UiCanvasSpace &space, WindowSize window) {
        if (window.width <= 0 || window.height <= 0 || space.scale <= 0.0f) {
            return space.layout_rect;
        }
        return render::Rect{
                -space.offset.x / space.scale,
                -space.offset.y / space.scale,
                static_cast<float>(window.width) / space.scale,
                static_cast<float>(window.height) / space.scale,
        };
    }

    void place_popups(Element &root, const render::Rect &bounds) {
        place_in(root, SpaceMap{}, glm::vec2{root.layout_rect.w, root.layout_rect.h}, bounds);
    }

    std::vector<OpenPopup> open_popups(Element &root) {
        std::vector<OpenPopup> out;
        if (!has_open_popup(root)) {
            return out;
        }
        std::vector<const Element *> ancestors;
        collect_in(root, ancestors, glm::vec2{root.layout_rect.w, root.layout_rect.h}, out);
        return out;
    }

    bool has_open_popup(Element &root) { return any_open_in(root); }

    Element *popup_at(Element &root, float x, float y) {
        const std::vector<OpenPopup> popups = open_popups(root);
        for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
            const Element &popup = *it->popup;
            render::Rect shown = hit_bounds(popup);
            shown.x += popup.popup_offset.x;
            shown.y += popup.popup_offset.y;
            if (rect_contains(shown, x, y)) {
                return it->popup;
            }
        }
        return nullptr;
    }

    bool contains_element(const Element &outer, const Element *inner) {
        if (&outer == inner) {
            return true;
        }
        for (const Element &child: outer.children) {
            if (contains_element(child, inner)) {
                return true;
            }
        }
        for (const Element &child: outer.generated_items) {
            if (contains_element(child, inner)) {
                return true;
            }
        }
        return false;
    }

} // namespace engine::ui
