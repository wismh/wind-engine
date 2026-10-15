#include "ui/pseudo_warning.h"

#include <algorithm>
#include <format>

namespace engine::ui {

    namespace {

        // True once the count of elements passes `limit`.
        bool exceeds(const Element &element, std::size_t &count, std::size_t limit) {
            if (++count > limit) {
                return true;
            }
            for (const std::vector<Element> *list: {&element.children, &element.generated_items}) {
                for (const Element &child: *list) {
                    if (child.kind != ElementKind::ItemTemplate && exceeds(child, count, limit)) {
                        return true;
                    }
                }
            }
            return false;
        }

        std::string layout_properties(const CssRule &rule) {
            std::string names;
            for (const CssDeclaration &declaration: rule.declarations) {
                if ((declaration_dirties(declaration) & kDirtyLayout) != 0 && !declaration.property.starts_with("--")) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += declaration.property;
                }
            }
            return names;
        }

    } // namespace

    void warn_layout_under_pseudo(const UiDocument &document, const Stylesheet &sheet, std::string_view document_name,
                                  std::unordered_set<std::string> &reported,
                                  const std::function<void(const std::string &)> &warn) {
        const StyleAnalysis &analysis = style_analysis(sheet);
        if (analysis.layout_pseudo_rules.empty()) {
            return;
        }
        std::size_t count = 0;
        if (!exceeds(document.root, count, kLayoutUnderPseudoWarnElements)) {
            return;
        }
        for (const std::uint32_t index: analysis.layout_pseudo_rules) {
            const CssRule &rule = sheet.rules[index];
            const std::string properties = layout_properties(rule);
            const std::string line = rule.line != 0 ? std::format("line {}", rule.line) : std::format("rule {}", index);
            std::string message = std::format(
                    "UI document {}: stylesheet {} changes layout under a pseudo-class ({}) in a tree of more than {} "
                    "elements; each time the pointer enters or leaves a matching element, layout runs over it and its "
                    "ancestors",
                    document_name, line, properties.empty() ? "custom property or var()" : properties,
                    kLayoutUnderPseudoWarnElements);
            if (reported.insert(message).second) {
                warn(message);
            }
        }
    }

} // namespace engine::ui
