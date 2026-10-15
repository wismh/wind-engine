#include "ui/element_index.h"

#include <vector>

namespace engine::ui {

    void index_subtree(ElementArena &arena, Element &element, ElementId parent, std::uint32_t position, bool generated,
                       bool fresh) {
        ElementArena::Slot *slot = fresh ? nullptr : arena.slot(element.handle);
        if (slot != nullptr && slot->seen == arena.sweep()) {
            // Already reached in this sweep: a second element carries the same id (an element copied by hand).
            slot = nullptr;
        }
        if (slot == nullptr) {
            element.handle = arena.allocate(parent, position, generated);
        } else {
            slot->parent = parent;
            slot->position = position;
            slot->generated = generated;
            slot->seen = arena.sweep();
        }
        element.parent = parent;
        for (std::size_t i = 0; i < element.children.size(); ++i) {
            index_subtree(arena, element.children[i], element.handle, static_cast<std::uint32_t>(i), false, fresh);
        }
        for (std::size_t i = 0; i < element.generated_items.size(); ++i) {
            index_subtree(arena, element.generated_items[i], element.handle, static_cast<std::uint32_t>(i), true,
                          fresh);
        }
    }

    void release_subtree(ElementArena &arena, const Element &element) {
        arena.release(element.handle);
        for (const Element &child: element.children) {
            release_subtree(arena, child);
        }
        for (const Element &item: element.generated_items) {
            release_subtree(arena, item);
        }
    }

    void UiDocument::reindex() {
        arena.begin_sweep();
        index_subtree(arena, root, ElementId{}, 0, false, false);
        arena.end_sweep();
    }

    namespace {

        struct Step {
            std::uint32_t position;
            bool generated;
        };

        // The steps from `id` up to the root (the nearest first), in one buffer that is reused: lookups and marking run
        // constantly and are never re-entered, so after the first one they cost no allocation.
        std::vector<Step> &step_scratch() {
            thread_local std::vector<Step> steps;
            steps.clear();
            return steps;
        }

        // Climbs the parent slots of `id` to the root, recording where each step sits in its parent. False when a
        // slot on the way is gone.
        bool climb(const ElementArena &arena, ElementId id, std::vector<Step> &steps, ElementId &top) {
            const ElementArena::Slot *slot = arena.slot(id);
            if (slot == nullptr) {
                return false;
            }
            top = id;
            while (!slot->parent.is_null()) {
                steps.push_back(Step{slot->position, slot->generated});
                top = slot->parent;
                slot = arena.slot(top);
                if (slot == nullptr) {
                    return false;
                }
            }
            return true;
        }

        template<typename ElementT, typename DocumentT>
        std::expected<ElementT *, ElementLookupError> find_in(DocumentT &document, ElementId id) {
            if (id.is_null()) {
                return std::unexpected(ElementLookupError::Null);
            }
            if (document.arena.slot(id) == nullptr) {
                return std::unexpected(ElementLookupError::Stale);
            }
            std::vector<Step> &steps = step_scratch();
            ElementId top;
            if (!climb(document.arena, id, steps, top)) {
                return std::unexpected(ElementLookupError::Misplaced);
            }
            ElementT *current = &document.root;
            if (current->handle != top) {
                return std::unexpected(ElementLookupError::Misplaced);
            }
            // Down again. A position past the end is a tree edited behind the arena's back, not a crash.
            for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
                auto &list = step->generated ? current->generated_items : current->children;
                if (step->position >= list.size()) {
                    return std::unexpected(ElementLookupError::Misplaced);
                }
                current = &list[step->position];
            }
            if (current->handle != id) {
                return std::unexpected(ElementLookupError::Misplaced);
            }
            return current;
        }

    } // namespace

    bool ancestor_chain(UiDocument &document, const Element &element, std::vector<Element *> &chain) {
        chain.clear();
        if (element.parent.is_null()) {
            return true;
        }
        std::vector<Step> &steps = step_scratch();
        ElementId top;
        if (!climb(document.arena, element.parent, steps, top) || document.root.handle != top) {
            return false;
        }
        Element *current = &document.root;
        chain.push_back(current);
        for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
            auto &list = step->generated ? current->generated_items : current->children;
            if (step->position >= list.size()) {
                chain.clear();
                return false;
            }
            current = &list[step->position];
            chain.push_back(current);
        }
        if (current->handle != element.parent) {
            chain.clear();
            return false;
        }
        return true;
    }

    std::expected<Element *, ElementLookupError> UiDocument::find(ElementId id) { return find_in<Element>(*this, id); }

    std::expected<const Element *, ElementLookupError> UiDocument::find(ElementId id) const {
        return find_in<const Element>(*this, id);
    }

} // namespace engine::ui
