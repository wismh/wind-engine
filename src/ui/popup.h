#pragma once

// docs/tech/modules/UI.md — Popup

#include <engine/ui/canvas.h>
#include <engine/ui/document.h>

#include <glm/vec2.hpp>

#include <optional>
#include <vector>

namespace engine::ui {

    // A Popup is laid out at its anchor (its parent) but shown above every canvas of the window, unclipped by
    // any ancestor. Layout leaves it at the anchor's top-left; place_popups works out where it is shown and
    // stores the difference in Element::popup_offset. The base walks (paint, hit_test, layout_boxes, ...)
    // skip every Popup child and visit the open ones afterwards, through open_popups, from that offset.

    // Layout point to shown point: shown = layout * scale + translate. Scroll and Viewport cameras compose into
    // it on the way down (child_map_of); an open popup starts a fresh one from its popup_offset.
    struct SpaceMap {
        float scale = 1.0f;
        glm::vec2 translate{0.0f, 0.0f};

        [[nodiscard]] render::Rect apply(const render::Rect &rect) const {
            return render::Rect{
                    translate.x + rect.x * scale,
                    translate.y + rect.y * scale,
                    rect.w * scale,
                    rect.h * scale,
            };
        }
    };

    // The map for `element`'s children, given the one for `element` itself (document.cpp).
    [[nodiscard]] SpaceMap child_map_of(const Element &element, const SpaceMap &map);

    // The basis layout gives `element`'s children: its content box, padding resolved against `parent_content`
    // (document.cpp).
    [[nodiscard]] glm::vec2 content_size_of(const Element &element, glm::vec2 parent_content);

    [[nodiscard]] inline bool is_open_popup(const Element &element) noexcept {
        return element.kind == ElementKind::Popup && element.open && !element.display_none;
    }

    // The rect a popup of `size` takes beside `anchor`, both in the canvas's layout units. `margin` is the
    // popup's resolved margin; only the side facing the anchor is used, as the gap. The popup flips to the
    // other side when it does not fit and that side has more room, then is pushed inside `bounds`.
    [[nodiscard]] render::Rect place_popup_rect(const render::Rect &anchor, glm::vec2 size, const BoxInsets &margin,
                                                PopupPlacement placement, const render::Rect &bounds);

    // The window in a canvas's layout units: the room its popups have. A window with no size yet (a test
    // world) is the canvas itself.
    [[nodiscard]] render::Rect popup_bounds(const UiCanvasSpace &space, WindowSize window);

    // Writes popup_offset on every open popup that is shown, outer popups before the ones nested in them.
    // Call after layout and again whenever scroll or a Viewport camera may have moved an anchor.
    void place_popups(Element &root, const render::Rect &bounds);

    struct OpenPopup {
        Element *popup = nullptr;
        // Document ancestors, root first, for selector matching.
        std::vector<const Element *> ancestors;
        // The anchor's content box, the percent basis layout used for the popup.
        glm::vec2 parent_content{};
    };

    // Open popups that are shown, in paint order: document order with siblings by z-index, and a popup
    // nested in another after it. Hit-testing walks the list from the back.
    [[nodiscard]] std::vector<OpenPopup> open_popups(Element &root);
    [[nodiscard]] bool has_open_popup(Element &root);

    // The topmost shown popup whose border box contains the layout point, or null.
    [[nodiscard]] Element *popup_at(Element &root, float x, float y);

    // The canvas of `window` with a shown popup under the window point, topmost canvas first, or nothing.
    // Uses the placement of the last input or paint pass (canvas.cpp).
    [[nodiscard]] std::optional<ecs::Entity> popup_canvas_at(ecs::World &world, WindowId window, glm::vec2 point);

    // Closes every open popup of the canvas and drops keyboard focus held inside it: for a canvas that stops being
    // shown (a dock panel whose tab went inactive).
    void release_canvas(ecs::World &world, ecs::Entity canvas);

        // True when `inner` is `outer` or anywhere in its subtree.
    [[nodiscard]] bool contains_element(const Element &outer, const Element *inner);

} // namespace engine::ui
