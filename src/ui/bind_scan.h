#pragma once

#include <engine/ui/document.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::ui {

// What a generated bind() registers a member as: `vm.property`, `vm.command`, or `vm.paint`.
enum class BindKind {
    Property,
    Command,
    Paint,
};

struct BindMember {
    std::string path;
    BindKind kind = BindKind::Property;
};

struct BindBinder {
    std::vector<BindMember> members;
    std::vector<std::pair<std::string, BindBinder>> nested;
};

// Every `{binding}` path the markup names, per data context: the document's own members, and one nested binder
// per ItemsControl's `items_source` for its ItemTemplate. Codegen emits bind() from it; parse_xml keeps the paths
// as UiDocument::binding_paths so a bind error can name the path.
[[nodiscard]] std::expected<BindBinder, UiError> scan_bind_tree(
        std::string_view xml, const UiIncludeResolver& resolve_include = {});

}
