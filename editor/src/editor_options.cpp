#include "editor_options.h"

#include <string_view>

namespace editor {

EditorOptions parse_editor_options(std::span<char* const> args) {
    EditorOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i] != nullptr ? std::string_view(args[i]) : std::string_view{};
        if (arg == "--project" && i + 1 < args.size() && args[i + 1] != nullptr) {
            // Narrow argv and a narrow std::filesystem::path use the same native code page.
            options.project = std::filesystem::path(std::string(args[++i]));
        } else if (arg == "--play") {
            options.play = true;
        } else {
            options.unknown.emplace_back(arg);
        }
    }
    return options;
}

}
