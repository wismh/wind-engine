#include "editor_options.h"

#include <string_view>

namespace editor {
namespace {

bool has_value(std::span<char* const> args, std::size_t index) {
    return index < args.size() && args[index] != nullptr;
}

// Narrow argv and a narrow std::filesystem::path use the same native code page.
std::filesystem::path path_of(const char* text) {
    return std::filesystem::path(std::string(text));
}

}

EditorOptions parse_editor_options(std::span<char* const> args) {
    EditorOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i] != nullptr ? std::string_view(args[i]) : std::string_view{};
        if (arg == "--project" && has_value(args, i + 1)) {
            options.project = path_of(args[++i]);
        } else if (arg == "--play") {
            options.play = true;
        } else if (arg == "--batch") {
            options.batch = true;
        } else if (arg == "--export") {
            options.export_game = true;
            // The directory is optional: another option, or nothing, after --export means the default.
            if (has_value(args, i + 1) && !std::string_view(args[i + 1]).starts_with("--")) {
                options.export_directory = path_of(args[++i]);
            }
        } else if (arg == "--log-file" && has_value(args, i + 1)) {
            options.log_file = path_of(args[++i]);
        } else {
            options.unknown.emplace_back(arg);
        }
    }
    return options;
}

std::string usage_error(const EditorOptions& options) {
    if (options.batch && !options.export_game) {
        return "--batch needs a command: --export [<dir>].";
    }
    if (options.batch && !options.project) {
        return "--batch needs --project <dir>.";
    }
    if (!options.batch && options.export_game) {
        return "--export runs without a window: add --batch.";
    }
    return {};
}

}
