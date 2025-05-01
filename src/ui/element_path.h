#pragma once

#include <engine/ui/document.h>

#include <cstddef>
#include <vector>

namespace engine::ui {

    // Index into Element::generated_items. Children use the plain index. Shared by scrollbar drag
    // and the UI inspector so a path written by one resolves in the other.
    inline constexpr std::size_t kGeneratedPathBit = std::size_t{1} << 31;

    [[nodiscard]] std::vector<std::size_t> find_element_path(const Element &root, const Element *target);

    [[nodiscard]] Element *resolve_element_path(Element &root, const std::vector<std::size_t> &path);

    // Nearest generated_owner on the path, including `target` itself. Null when every step is a
    // static child. The inspector stores this so a virtualized row can be found after its index moves.
    [[nodiscard]] const void *path_generated_owner(const Element &root, const std::vector<std::size_t> &path);

    // Like resolve_element_path, but a generated step whose item is not `owner` searches that
    // parent's generated_items for `owner` before falling back to the stored index. `owner` null
    // is a plain resolve. Returns null when the path does not exist and the owner is not live.
    [[nodiscard]] Element *resolve_inspector_element(Element &root, const std::vector<std::size_t> &path,
                                                     const void *owner);

} // namespace engine::ui
