#pragma once

#include <engine/core/file_dialog.h>
#include <engine/core/window_desc.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine {

class Worlds;

// Hands open-file dialog answers from whatever thread the platform calls back on to the main thread.
// `begin` and `deliver` run on the main thread. `complete` may run on any thread.
class FileDialogQueue {
public:
    // A fresh request id that remembers `owner`.
    [[nodiscard]] FileDialogRequest begin(WindowId owner);

    // Records the answer. An empty `path` means cancelled or failed.
    void complete(FileDialogRequest request, std::optional<std::filesystem::path> path);

    // Sends every recorded answer as FileDialogResultEvent to the world bound to its owner window, then
    // forgets it. An answer whose owner has no world is dropped.
    void deliver(Worlds& worlds);

private:
    struct Answer {
        FileDialogRequest request{};
        std::optional<std::filesystem::path> path;
    };

    std::mutex mutex_;
    std::vector<Answer> answers_;
    std::unordered_map<FileDialogRequest, WindowId> owners_;
    std::uint32_t next_ = 1;
};

}
