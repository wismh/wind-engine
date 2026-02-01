#include <engine/ui/element_arena.h>

namespace engine::ui {

    ElementId ElementArena::allocate(ElementId parent, std::uint32_t position, bool generated) {
        std::uint32_t index = 0;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot &slot = slots_[index];
        slot.parent = parent;
        slot.position = position;
        slot.generated = generated;
        slot.live = true;
        slot.seen = sweep_;
        ++live_;
        return ElementId{index + 1, slot.generation};
    }

    void ElementArena::release(ElementId id) {
        Slot *slot = this->slot(id);
        if (slot == nullptr) {
            return;
        }
        slot->live = false;
        slot->parent = {};
        ++slot->generation;
        if (slot->generation == 0) {
            // 2^32 reuses of one slot: skip the generation 0 a default id carries.
            slot->generation = 1;
        }
        free_.push_back(id.index - 1);
        --live_;
    }

    const ElementArena::Slot *ElementArena::slot(ElementId id) const {
        if (id.index == 0 || id.index > slots_.size()) {
            return nullptr;
        }
        const Slot &slot = slots_[id.index - 1];
        return slot.live && slot.generation == id.generation ? &slot : nullptr;
    }

    ElementArena::Slot *ElementArena::slot(ElementId id) {
        return const_cast<Slot *>(static_cast<const ElementArena *>(this)->slot(id));
    }

    void ElementArena::mark(ElementId id) {
        if (Slot *slot = this->slot(id)) {
            slot->seen = sweep_;
        }
    }

    std::size_t ElementArena::end_sweep() {
        std::size_t freed = 0;
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            Slot &slot = slots_[index];
            if (slot.live && slot.seen != sweep_) {
                release(ElementId{index + 1, slot.generation});
                ++freed;
            }
        }
        return freed;
    }

} // namespace engine::ui
