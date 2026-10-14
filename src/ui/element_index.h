#pragma once

// docs/tech/modules/UI.md#element-identity

#include <engine/ui/document.h>

namespace engine::ui {

    // Gives `element` and everything under it (children, generated items) an id and a parent, in `arena`. With `fresh`
    // every element gets a new id, whatever it carries: the subtree is a copy of another (a row cloned from its
    // template) and its ids belong to the original. Without it (only inside an `ElementArena` sweep,
    // `UiDocument::reindex`) an element keeps its id and only its place is updated; a second element with an id already
    // reached in the sweep gets a new one.
    void index_subtree(ElementArena &arena, Element &element, ElementId parent, std::uint32_t position, bool generated,
                       bool fresh);

    // Frees the slot of `element` and of everything under it.
    void release_subtree(ElementArena &arena, const Element &element);

} // namespace engine::ui
