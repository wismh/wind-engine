#pragma once

#include <engine/process/process_desc.h>

#include <expected>
#include <memory>
#include <optional>

// Linux and macOS. Android and Emscripten cannot start programs.
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#define ENGINE_PROCESS_POSIX 1
#endif

namespace engine {

struct ProcessCallState;

#if defined(ENGINE_PROCESS_POSIX)

// A child started for ProcessLauncher::run: the process, the process group it and its own children run in, and the
// thread that reads their shared output pipe into the call's state. Same shape as WindowsProcess.
class PosixProcess {
public:
    // Starts `desc` as the leader of a new process group. The reader thread starts at once.
    [[nodiscard]] static std::expected<std::unique_ptr<PosixProcess>, ProcessError> start(
            const ProcessDesc& desc, std::shared_ptr<ProcessCallState> state);

    virtual ~PosixProcess() = default;

    // Kills every process in the group. Safe to repeat.
    virtual void end() = 0;

    // The exit code once the program exited, else nullopt. A program that a signal ended has 128 + the signal, as a
    // shell reports it. The first time it sees the exit it kills the processes the program left running, which would
    // otherwise hold the pipe open.
    [[nodiscard]] virtual std::optional<int> poll_exit() = 0;

    // Waits for the reader thread. Call it once the state's output closed, or after `end`.
    virtual void join() = 0;
};

// Starts `desc` on its own: a new session, no pipe, standard streams on /dev/null. The program is not this process's
// child, so it outlives it and leaves no zombie.
[[nodiscard]] std::expected<void, ProcessError> launch_posix_process(const ProcessDesc& desc);

#endif

}
