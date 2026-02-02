#pragma once

// docs/tech/modules/UI.md#hover-and-layout

#include <engine/ui/document.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace engine::ui {

    // A tree larger than this many elements makes a layout-affecting pseudo-class rule worth a warning: every time the
    // pointer enters or leaves a matching element, layout runs over the chain above it.
    inline constexpr std::size_t kLayoutUnderPseudoWarnElements = 500;

    // For every rule of `sheet` that has a pseudo-class and a layout-affecting property (width, padding, display, ...),
    // calls `warn` once with a message naming `document_name` and the rule's line in the stylesheet, when the
    // tree has more than `kLayoutUnderPseudoWarnElements` elements. `reported` remembers what was already said (the
    // message text), so a rule is named once per document however often this is called.
    void warn_layout_under_pseudo(const UiDocument &document, const Stylesheet &sheet, std::string_view document_name,
                                  std::unordered_set<std::string> &reported,
                                  const std::function<void(const std::string &)> &warn);

} // namespace engine::ui
