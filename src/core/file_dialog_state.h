#pragma once

#include "core/call_completions.h"

#include <engine/core/file_dialog.h>

#include <optional>

namespace engine {

// Shared by one FileDialogCall, the completion queue, and the platform dialog's callback. Main thread
// only: the callback, which may run on another thread, hands its answer to the queue and touches nothing
// here.
struct FileDialogState {
    std::optional<FileDialogResult> result;

    [[nodiscard]] bool cancelled() const {
        return cancelled_;
    }

    void cancel() {
        cancelled_ = true;
    }

private:
    bool cancelled_ = false;
};

// Dialog answers waiting for the main thread.
using FileDialogCompletions = CallCompletions<FileDialogState, FileDialogResult>;

}
