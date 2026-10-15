#pragma once

// docs/tech/modules/UI.md#element-identity

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace engine::ui {

    // The identity of one `Element` of one `UiDocument`: a slot of the document's `ElementArena` and the generation
    // that slot is on. It is not an index into `children` and not a pointer. It survives everything that keeps the
    // element (an ItemsControl reconcile that reuses a row, a move of the document), and a freed slot's generation is
    // never handed out again, so an id of a destroyed element can never name a later one. Look it up with
    // `UiDocument::find`. Two ids are the same element when both fields are equal. The default id is null and names
    // nothing.
    struct ElementId {
        // The arena slot plus one, so 0 is the null id.
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] constexpr bool is_null() const noexcept { return index == 0; }
        [[nodiscard]] constexpr bool operator==(const ElementId &) const noexcept = default;
    };

    // Why `UiDocument::find` gave no element.
    enum class ElementLookupError : std::uint8_t {
        // The id is null.
        Null,
        // The element is gone: its slot was freed (a row that left the window, a rebuilt document) or reused since.
        Stale,
        // The id is alive, but the tree no longer matches the arena: children were edited by hand after the last
        // `UiDocument::reindex`.
        Misplaced,
    };

    [[nodiscard]] constexpr std::string_view element_lookup_error_name(ElementLookupError error) noexcept {
        switch (error) {
            case ElementLookupError::Null:
                return "null element id";
            case ElementLookupError::Stale:
                return "stale element id";
            case ElementLookupError::Misplaced:
                return "element id out of step with the tree (children edited without reindex)";
        }
        return "";
    }

} // namespace engine::ui

template<>
struct std::hash<engine::ui::ElementId> {
    std::size_t operator()(const engine::ui::ElementId &id) const noexcept {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(id.generation) << 32) | id.index);
    }
};
