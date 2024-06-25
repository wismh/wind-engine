#pragma once

#include <engine/ui/document.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::ui {

struct BindMember {
    std::string path;
    bool is_command = false;
};

struct BindBinder {
    std::vector<BindMember> members;
    std::vector<std::pair<std::string, BindBinder>> nested;
};

[[nodiscard]] std::expected<BindBinder, UiError> scan_bind_tree(std::string_view xml);

}
