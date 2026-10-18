#include "ui/picture_paint.h"

namespace engine::ui {

    PictureContext &picture_context() {
        thread_local PictureContext context;
        return context;
    }

    // Not painted now: nothing under it changes what a picture above it shows. A closed popup is painted by nobody.
    bool element_hidden(const Element &e) noexcept {
        return e.display_none || e.hidden_by_if || !e.visible || (is_popup_kind(e.kind) && !e.open);
    }

    // Elements that keep a picture of their own: the root, anything that scrolls, a camera, a popup, and what the
    // stylesheet marks with `repaint-boundary`. Decided from the element alone, so a pass that looks for marks can
    // tell where one picture ends and the next begins without a style.
    bool is_boundary_element(const Element &e) noexcept {
        return e.parent.is_null() || e.repaint_boundary || e.kind == ElementKind::Viewport ||
               e.kind == ElementKind::ScrollView || is_popup_kind(e.kind) || is_scrollable(e);
    }

    bool contains_rect(const render::Rect &outer, const render::Rect &inner) noexcept {
        return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
               inner.y + inner.h <= outer.y + outer.h;
    }


    const std::vector<Element> &paint_children(const Element &e) {
        return e.kind == ElementKind::ItemsControl ? e.generated_items : e.children;
    }

    // A plain element of a boundary's region (not a boundary itself, not hidden) holds a paint mark: the
    // boundary's picture shows something that changed. Looks only along the marks.
    bool plain_dirty(const Element &e) {
        if (e.kind == ElementKind::ItemTemplate || element_hidden(e) || is_boundary_element(e)) {
            return false;
        }
        if ((e.dirty_self & kPaintMarks) != 0) {
            return true;
        }
        if ((e.dirty_below & kPaintMarks) == 0) {
            return false;
        }
        for (const Element &child: paint_children(e)) {
            if (((child.dirty_self | child.dirty_below) & kPaintMarks) != 0 && plain_dirty(child)) {
                return true;
            }
        }
        return false;
    }

    bool region_dirty(const std::vector<Element> &children) {
        for (const Element &child: children) {
            if (((child.dirty_self | child.dirty_below) & kPaintMarks) != 0 && plain_dirty(child)) {
                return true;
            }
        }
        return false;
    }

    // Clears the marks of a boundary's region once its pictures are up to date, and says what is left below: a
    // nested boundary keeps its own marks until it is brought up to date, a hidden element keeps them until it is
    // shown (showing it marks it again).
    static std::uint8_t clear_plain_marks(Element &e) {
        if (e.kind == ElementKind::ItemTemplate || element_hidden(e)) {
            return 0;
        }
        if (is_boundary_element(e)) {
            return static_cast<std::uint8_t>((e.dirty_self | e.dirty_below) & kPaintMarks);
        }
        std::uint8_t remaining = 0;
        const auto each = [&](std::vector<Element> &list) {
            for (Element &child: list) {
                if (((child.dirty_self | child.dirty_below) & kPaintMarks) != 0) {
                    remaining |= clear_plain_marks(child);
                }
            }
        };
        e.dirty_self &= static_cast<std::uint8_t>(~kPaintMarks);
        each(e.children);
        each(e.generated_items);
        e.dirty_below = static_cast<std::uint8_t>((e.dirty_below & ~kPaintMarks) | remaining);
        return remaining;
    }

    void clear_region_marks(Element &boundary, std::vector<Element> &children) {
        std::uint8_t remaining = 0;
        for (Element &child: children) {
            if (((child.dirty_self | child.dirty_below) & kPaintMarks) != 0) {
                remaining |= clear_plain_marks(child);
            }
        }
        boundary.dirty_self &= static_cast<std::uint8_t>(~kPaintMarks);
        boundary.dirty_below = static_cast<std::uint8_t>((boundary.dirty_below & ~kPaintMarks) | remaining);
    }

    // What a recording notes: the picture being recorded will not be reusable (a blinking caret, an `IPaint` that
    // cannot say when it changed), or depends on an `IPaint`'s version.
    void note_volatile() {
        PictureContext &context = picture_context();
        if (context.recording != nullptr) {
            context.recording->volatile_part = true;
        }
    }

    void note_paint(const Element &carrier) {
        const IPaint &paint = *carrier.paint;
        PictureContext &context = picture_context();
        if (context.recording == nullptr) {
            return;
        }
        const std::uint64_t version = paint.version();
        if (version == IPaint::kVersionUnknown) {
            context.recording->volatile_part = true;
        } else {
            context.recording->watches.push_back(PictureWatch{carrier.handle, &paint, version});
        }
    }

} // namespace engine::ui
