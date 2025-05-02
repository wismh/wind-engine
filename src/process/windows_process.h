#pragma once

#include <engine/process/process_desc.h>

#include <expected>
#include <memory>
#include <optional>

namespace engine {

struct ProcessCallState;

#if defined(_WIN32)

// A child started for ProcessLauncher::run: the process, the job object it and its own children run in, and the
// thread that reads their shared output pipe into the call's state. Win32 types stay in the .cpp.
class WindowsProcess {
public:
    // Starts `desc` suspended inside a new job, then lets it run. The reader thread starts at once.
    [[nodiscard]] static std::expected<std::unique_ptr<WindowsProcess>, ProcessError> start(
            const ProcessDesc& desc, std::shared_ptr<ProcessCallState> state);

    virtual ~WindowsProcess() = default;

    // Ends every process in the job. Safe to repeat.
    virtual void end() = 0;

    // The exit code once the program exited, else nullopt. The first time it sees the exit it ends the processes
    // the program left running, which would otherwise hold the pipe open.
    [[nodiscard]] virtual std::optional<int> poll_exit() = 0;

    // Waits for the reader thread. Call it once the state's output closed, or after `end`.
    virtual void join() = 0;
};

// Starts `desc` on its own: no pipe, no console, no inherited handle, out of this process's job when that job
// allows it.
[[nodiscard]] std::expected<void, ProcessError> launch_windows_process(const ProcessDesc& desc);

#endif

}
