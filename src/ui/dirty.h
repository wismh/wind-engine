#pragma once

// docs/tech/modules/UI.md#dirty-flags

#include <engine/ui/document.h>

#include <cstdint>

namespace engine::ui {

    // Marks `mask` (`kDirtyStyle`, `kDirtyLayout`, `kDirtyPaint` of stylesheet.h) on `element` and records on its
    // ancestors, found by `parent` id, that something below them needs it, so the pass that consumes a bit reaches the
    // element from the root without visiting the rest of the tree. A bit already recorded on an ancestor ends the
    // climb for that bit. Layout also ends at a relayout boundary (`is_relayout_boundary`): the boundary is queued in
    // `document.layout_roots` and laid out from where it is. `own_box`: the change can alter this element's own size
    // or margin (its own style changed), so layout marking passes through even when the element itself is a boundary.
    // An element that has no id yet (a tree nobody indexed) marks only itself; every pass treats a document that was
    // never laid out as dirty everywhere.
    void mark_dirty(UiDocument &document, Element &element, std::uint8_t mask, bool own_box = false);

    // A brand-new subtree (a row cloned from its template): every element wants every pass, and the ancestors are told.
    void mark_new_subtree(UiDocument &document, Element &root);

    // True when nothing below `element` can change its size or position: both sizes are fixed px, it is laid out
    // in flow or absolutely (not relative), and it has been laid out once, so its layout can start from the rect and
    // the parent content size it was last given.
    [[nodiscard]] bool is_relayout_boundary(const Element &element);

    // Clears `mask` from `element` and, going only where `dirty_below` says something is flagged, from everything under
    // it. A pass that consumed the bits calls it.
    void clear_dirty(Element &element, std::uint8_t mask);

    // The pseudo-classes an element can be in. Their bits are the stylesheet's `kPseudo*`.
    [[nodiscard]] std::uint8_t pseudo_state_of(const Element &element) noexcept;

    // Sets one pseudo-class of `element` to `value`. When it changes, marks what the stylesheet says depends on it:
    // the element for paint, and for layout (restyle) the element and the descendants that a rule can match through
    // it (`A:hover B`) only where some rule that also lays out tests that bit; a rule that only paints never sets the
    // layout flag. `sheet` null (no stylesheet) marks paint only. Returns whether the state changed.
    bool set_pseudo(UiDocument &document, Element &element, std::uint8_t bit, bool value, const Stylesheet *sheet);

    // The ancestors of `element`, root first, found by parent id; empty when it has none or an id is stale.
    [[nodiscard]] std::vector<const Element *> ancestors_of(const UiDocument &document, const Element &element);

} // namespace engine::ui
