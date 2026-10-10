#pragma once

#include "project_build.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace engine {
class IProcessLauncher;
}

namespace editor {

// Exit codes of `wind_editor --batch`.
inline constexpr int kBatchOk = 0;
// The project, the SDK, the build, or the copy failed.
inline constexpr int kBatchFailed = 1;
// The command line cannot run.
inline constexpr int kBatchUsage = 2;

// What `wind_editor --batch --export` runs on.
struct BatchSetup {
    // The SDK root: the parent of the directory of wind_editor (`<sdk>/bin`).
    std::filesystem::path sdk;
    // The project directory (--project).
    std::filesystem::path project;
    // Where the game goes (--export <dir>); `<project>/export/<target>` when empty.
    std::optional<std::filesystem::path> export_directory;
};

// The editor's Export without a window: reads the SDK and the project, checks them the way the editor does when it
// opens a project, builds the standalone executable, and copies it. The caller pumps it: `start`, then `poll` once
// per tick (after the process launcher's own poll) until it returns the exit code. Every line of progress and of
// cmake's output goes to the sink; failures also go to engine::log.
class BatchRun {
public:
    using LineSink = std::function<void(const std::string& line)>;

    BatchRun(engine::IProcessLauncher& processes, LineSink sink);

    BatchRun(const BatchRun&) = delete;
    BatchRun& operator=(const BatchRun&) = delete;

    // Starts the export. Returns the exit code when it ended at once (the project or the SDK did not fit).
    [[nodiscard]] std::optional<int> start(BatchSetup setup);

    // Returns the exit code once, when the run ended.
    [[nodiscard]] std::optional<int> poll();

private:
    [[nodiscard]] int fail(const std::string& message);

    ProjectBuild build_;
    LineSink sink_;
    std::filesystem::path export_directory_;
    bool running_ = false;
};

}
