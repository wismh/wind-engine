#pragma once

// docs/tech/features/Windowing.md

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace engine {

// One row of an open-file dialog's type list. `pattern` is a semicolon-separated list of extensions
// without dots ("dll", "png;jpg"), or "*" for every file.
struct FileFilter {
    std::string name;
    std::string pattern;
};

// The dialog's answer. `path` is empty when the user cancelled or the platform dialog failed.
struct FileDialogResult {
    std::optional<std::filesystem::path> path;
};

struct FileDialogState;

// One open-file dialog, owned by whoever asked for it (`IWindowControl::request_open_file`). Move-only.
// Destroying a call that is still pending cancels it.
//
// The platform dialog cannot be closed from code: a cancelled call only drops the answer, and the dialog
// stays on screen until the user closes it.
//
// The answer becomes visible during the poll at the start of a frame, so every system of that frame sees
// the same state. Main thread only.
class FileDialogCall {
public:
    FileDialogCall() = default;
    // Engine side: `IWindowControl` implementations build calls from their own state.
    explicit FileDialogCall(std::shared_ptr<FileDialogState> state);

    FileDialogCall(const FileDialogCall&) = delete;
    FileDialogCall& operator=(const FileDialogCall&) = delete;
    FileDialogCall(FileDialogCall&& other) noexcept = default;
    FileDialogCall& operator=(FileDialogCall&& other) noexcept;
    ~FileDialogCall();

    // A call that already holds `result`. For fakes of IWindowControl.
    [[nodiscard]] static FileDialogCall resolved(FileDialogResult result);

    // Shown, not cancelled, and no answer yet.
    [[nodiscard]] bool pending() const;

    // The answer once, then the call is empty. nullopt while pending, after cancel, and on an empty call.
    [[nodiscard]] std::optional<FileDialogResult> take();

    // Drops the answer that would arrive. The dialog stays open (see above). An empty or answered call
    // ignores it.
    void cancel();

private:
    std::shared_ptr<FileDialogState> state_;
};

}
