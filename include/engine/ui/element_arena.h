#pragma once

// docs/tech/modules/UI.md#element-identity

#include <engine/ui/element_id.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::ui {

    // The slot table behind `ElementId`, one per `UiDocument`. A slot says where its element hangs in the tree (the
    // parent's id, the position in the parent's `children` or `generated_items`), never an `Element` address, so the
    // elements stay plain `std::vector<Element>` values that may move and relocate freely, and so a copied or moved
    // document keeps valid ids. `UiDocument::find` walks parent slots up and the child positions down, and checks the
    // element it reaches carries the id, so an out-of-date slot is an error and never a stale pointer.
    class ElementArena {
    public:
        struct Slot {
            // Bumped when the slot is freed. A slot starts on generation 1.
            std::uint32_t generation = 1;
            ElementId parent{};
            std::uint32_t position = 0;
            // The element is in the parent's `generated_items`, not its `children`.
            bool generated = false;
            bool live = false;
            // The sweep that last reached this slot (`UiDocument::reindex`).
            std::uint32_t seen = 0;
        };

        // A fresh live slot, reusing a freed one on its next generation.
        [[nodiscard]] ElementId allocate(ElementId parent, std::uint32_t position, bool generated);

        // Frees the slot and bumps its generation. An id that is null or not live is ignored.
        void release(ElementId id);

        [[nodiscard]] bool alive(ElementId id) const { return slot(id) != nullptr; }

        // The slot `id` names, or null when `id` is null, out of range, freed, or of an older generation.
        [[nodiscard]] const Slot *slot(ElementId id) const;
        [[nodiscard]] Slot *slot(ElementId id);

        [[nodiscard]] std::size_t live_count() const { return live_; }

        // A mark-and-sweep over the whole table: `begin_sweep`, `mark` every id still in the tree, `end_sweep` frees
        // the live slots that were not marked and returns how many.
        void begin_sweep() { ++sweep_; }
        // The current sweep's number, what a slot's `seen` is set to when it is reached.
        [[nodiscard]] std::uint32_t sweep() const { return sweep_; }
        void mark(ElementId id);
        std::size_t end_sweep();

    private:
        std::vector<Slot> slots_;
        std::vector<std::uint32_t> free_;
        std::size_t live_ = 0;
        std::uint32_t sweep_ = 0;
    };

} // namespace engine::ui
