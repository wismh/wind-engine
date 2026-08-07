#include "ui/dirty.h"

#include "ui/element_index.h"
#include "ui/painter.h"

#include <algorithm>
#include <functional>

namespace engine::ui {

    namespace {

        bool fixed_px(const std::optional<Length> &length) {
            return length.has_value() && length->unit == LengthUnit::Px && length->calc.empty();
        }

        void queue_root(UiDocument &document, ElementId id) {
            if (std::ranges::find(document.layout_roots, id) == document.layout_roots.end()) {
                document.layout_roots.push_back(id);
            }
        }

        void walk_descendants(const Element &parent, std::vector<const Element *> &ancestors,
                              const std::function<void(Element &, const std::vector<const Element *> &)> &visit) {
            ancestors.push_back(&parent);
            const auto each = [&](const std::vector<Element> &list) {
                for (const Element &child: list) {
                    if (child.kind == ElementKind::ItemTemplate) {
                        continue;
                    }
                    visit(const_cast<Element &>(child), ancestors);
                    walk_descendants(child, ancestors, visit);
                }
            };
            each(parent.children);
            each(parent.generated_items);
            ancestors.pop_back();
        }

        // True when `rule` sets a property that children inherit (the list `seed_inherited` in paint.cpp copies).
        bool declares_inherited(const CssRule &rule) {
            static constexpr std::string_view kInherited[] = {
                    "color",       "font-family", "font-size",  "line-height",     "text-align",
                    "white-space", "visibility",  "user-select", "selection-color",
            };
            return std::ranges::any_of(rule.declarations, [](const CssDeclaration &declaration) {
                return std::ranges::find(kInherited, declaration.property) != std::end(kInherited);
            });
        }

    } // namespace

    bool is_relayout_boundary(const Element &element) {
        return fixed_px(element.width) && fixed_px(element.height) && element.position != PositionMode::Relative &&
               !is_popup_kind(element.kind) && element.kind != ElementKind::ItemTemplate &&
               element.layout_runs > 0 && !element.display_none && !element.is_virtualization_spacer;
    }

    void mark_dirty(UiDocument &document, Element &element, std::uint8_t mask, bool own_box) {
        element.dirty_self |= mask;
        ++document.epoch;
        std::uint8_t climbing = mask;
        if ((climbing & kDirtyLayout) != 0 && !own_box && is_relayout_boundary(element)) {
            // A change inside a fixed-size element cannot move anything outside it.
            queue_root(document, element.handle);
            climbing &= static_cast<std::uint8_t>(~kDirtyLayout);
        }
        if (climbing == 0 || element.parent.is_null()) {
            return;
        }
        thread_local std::vector<Element *> chain;
        if (!ancestor_chain(document, element, chain)) {
            // The id no longer leads from the root to this element (a tree edited behind the arena's back): nothing can
            // say what is above it, so the next layout sync does everything rather than miss a mark.
            document.layout_computed_once = false;
            return;
        }
        for (auto parent = chain.rbegin(); parent != chain.rend() && climbing != 0; ++parent) {
            const std::uint8_t added = climbing & static_cast<std::uint8_t>(~(*parent)->dirty_below);
            if (added == 0) {
                break;
            }
            (*parent)->dirty_below |= added;
            climbing = added;
            if ((climbing & kDirtyLayout) != 0 && is_relayout_boundary(**parent)) {
                queue_root(document, (*parent)->handle);
                climbing &= static_cast<std::uint8_t>(~kDirtyLayout);
            }
        }
    }

    void mark_new_subtree(UiDocument &document, Element &root) {
        const std::uint8_t all = kDirtyStyle | kDirtyLayout | kDirtyPaint;
        const auto mark_all = [&](const auto &self, Element &element) -> void {
            element.dirty_self |= all;
            const bool has_children = !element.children.empty() || !element.generated_items.empty();
            if (has_children) {
                element.dirty_below |= all;
            }
            for (Element &child: element.children) {
                self(self, child);
            }
            for (Element &item: element.generated_items) {
                self(self, item);
            }
        };
        mark_all(mark_all, root);
        // The new subtree's own layout cannot start from a boundary: it was never laid out.
        mark_dirty(document, root, all, true);
    }

    void clear_dirty(Element &element, std::uint8_t mask) {
        const std::uint8_t clear = static_cast<std::uint8_t>(~mask);
        const bool descend = (element.dirty_below & mask) != 0;
        element.dirty_self &= clear;
        element.dirty_below &= clear;
        if (!descend) {
            return;
        }
        for (Element &child: element.children) {
            if ((child.dirty_self | child.dirty_below) & mask) {
                clear_dirty(child, mask);
            }
        }
        for (Element &item: element.generated_items) {
            if ((item.dirty_self | item.dirty_below) & mask) {
                clear_dirty(item, mask);
            }
        }
    }

    std::uint8_t pseudo_state_of(const Element &element) noexcept {
        return static_cast<std::uint8_t>((element.hovered ? kPseudoHover : 0) | (element.pressed ? kPseudoPressed : 0) |
                                         (element.disabled ? kPseudoDisabled : 0) |
                                         (element.focused ? kPseudoFocus : 0) | (element.checked ? kPseudoChecked : 0));
    }

    std::vector<const Element *> ancestors_of(const UiDocument &document, const Element &element) {
        std::vector<const Element *> chain;
        ElementId id = element.parent;
        while (!id.is_null()) {
            const auto found = document.find(id);
            if (!found) {
                return {};
            }
            chain.push_back(*found);
            id = (*found)->parent;
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    bool set_pseudo(UiDocument &document, Element &element, std::uint8_t bit, bool value, const Stylesheet *sheet) {
        bool *state = nullptr;
        switch (bit) {
            case kPseudoHover:
                state = &element.hovered;
                break;
            case kPseudoPressed:
                state = &element.pressed;
                break;
            case kPseudoDisabled:
                state = &element.disabled;
                break;
            case kPseudoFocus:
                state = &element.focused;
                break;
            case kPseudoChecked:
                state = &element.checked;
                break;
            default:
                return false;
        }
        if (*state == value) {
            return false;
        }
        *state = value;
        // The element's own paint reads its state in every case.
        mark_dirty(document, element, kDirtyPaint);
        if (sheet == nullptr) {
            return true;
        }
        const StyleAnalysis &analysis = style_analysis(*sheet);
        if (analysis.pseudo_rules.empty()) {
            return true;
        }
        const std::vector<const Element *> own_ancestors = ancestors_of(document, element);
        bool restyle_self = false;
        for (const std::uint32_t index: analysis.layout_pseudo_rules) {
            const CssRule &rule = sheet->rules[index];
            if ((rule.subject_pseudo & bit) != 0 && rule_could_match(rule, element, own_ancestors)) {
                restyle_self = true;
                break;
            }
        }
        if (restyle_self) {
            // The layout style of this element reads its state: restyle it, and let the geometry diff say whether
            // layout has anything to do.
            mark_dirty(document, element, kDirtyStyle | kDirtyPaint, true);
        }
        std::vector<const Element *> chain = own_ancestors;
        // `.row:hover { color: ... }`: what the descendants inherit changes with the state, so they repaint. A rule that
        // also lays out reaches their layout style through the restyle of this element (its inherited stamp moves).
        for (const std::uint32_t index: analysis.pseudo_rules) {
            const CssRule &rule = sheet->rules[index];
            if ((rule.subject_pseudo & bit) != 0 && declares_inherited(rule) &&
                rule_could_match(rule, element, own_ancestors)) {
                walk_descendants(element, chain,
                                 [&](Element &descendant, const std::vector<const Element *> &) {
                                     mark_dirty(document, descendant, kDirtyPaint, true);
                                 });
                break;
            }
        }
        if ((analysis.ancestor_pseudo_mask & bit) == 0) {
            return true;
        }
        // `A:hover B`: the descendants a rule can match through this element.
        walk_descendants(
                element, chain, [&](Element &descendant, const std::vector<const Element *> &descendant_ancestors) {
                    for (const std::uint32_t index: analysis.pseudo_rules) {
                        const CssRule &rule = sheet->rules[index];
                        if ((rule.ancestor_pseudo & bit) == 0 ||
                            !rule_could_match(rule, descendant, descendant_ancestors)) {
                            continue;
                        }
                        mark_dirty(document, descendant,
                                   (rule.dirties & kDirtyLayout) != 0 ? kDirtyStyle | kDirtyPaint : kDirtyPaint, true);
                        break;
                    }
                });
        return true;
    }

} // namespace engine::ui
