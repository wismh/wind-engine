#include "editor_selection.h"

#include <utility>

namespace editor {

void EditorSelection::select(SelectionTarget target) {
    target_ = std::move(target);
    ++revision_;
}

void EditorSelection::clear() {
    select(std::monostate{});
}

const SelectionTarget& EditorSelection::target() const {
    return target_;
}

std::uint64_t EditorSelection::revision() const {
    return revision_;
}

}
