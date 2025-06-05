#pragma once

// docs/tech/features/Windowing.md

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace engine {

// One row of an open-file dialog's type list. `pattern` is a semicolon-separated list of extensions
// without dots ("dll", "png;jpg"), or "*" for every file.
struct FileFilter {
    std::string name;
    std::string pattern;
};

// Names one `IWindowControl::request_open_file` call. Its result event carries the same value.
enum class FileDialogRequest : std::uint32_t {};

// Sent to the world bound to the dialog's owner window, during the frame's poll, once per request.
// `path` is empty when the user cancelled or the platform dialog failed. A result whose owner window
// has no world by then is dropped.
struct FileDialogResultEvent {
    FileDialogRequest request{};
    std::optional<std::filesystem::path> path;
};

}
