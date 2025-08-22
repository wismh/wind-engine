#pragma once

#include "asset_selection.h"
#include "ui_element_selection.h"

#include <cstdint>
#include <variant>

namespace editor {

using SelectionTarget = std::variant<std::monostate, AssetSelection, UiElementSelection>;

// What the Inspector tab shows: the last thing selected anywhere in the editor. The Project tab selects a
// file or folder, the UI Tree tab (or a pick click in the game) selects an element; the newest one wins.
// Selecting in one panel does not clear another panel's own highlight.
class EditorSelection {
public:
    // Replaces the selection, even with an equal one, and counts it: the Inspector reads a file again when
    // the Project tab selects it again or rescans it.
    void select(SelectionTarget target);
    void clear();

    [[nodiscard]] const SelectionTarget& target() const;
    // Moves on every select and clear.
    [[nodiscard]] std::uint64_t revision() const;

private:
    SelectionTarget target_;
    std::uint64_t revision_ = 0;
};

}
