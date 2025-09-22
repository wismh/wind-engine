#include <engine/process/process_launcher.h>

#include "process/process_call_state.h"

#include "process/posix_process.h"
#include "process/windows_process.h"

// The platform's process backend: both classes have the same shape.
#if defined(_WIN32)
#define ENGINE_PROCESS_NATIVE 1
#elif defined(ENGINE_PROCESS_POSIX)
#define ENGINE_PROCESS_NATIVE 1
#endif

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace engine {

#if defined(_WIN32)
using NativeProcess = WindowsProcess;
#elif defined(ENGINE_PROCESS_POSIX)
using NativeProcess = PosixProcess;
#endif

struct ProcessLauncher::Impl {
    // Calls whose program never started. Their error reaches them on the next poll.
    struct Failed {
        std::shared_ptr<ProcessCallState> state;
        ProcessError error;
    };
    std::vector<Failed> failed;

#if defined(ENGINE_PROCESS_NATIVE)
    struct Running {
        std::shared_ptr<ProcessCallState> state;
        std::unique_ptr<NativeProcess> process;
        std::optional<int> exit_code;
        bool done = false;
    };
    std::vector<Running> running;

    void poll_running() {
        for (Running& child : running) {
            const bool cancelled = child.state->cancelled();
            if (cancelled) {
                child.process->end();
            } else if (!child.exit_code.has_value()) {
                child.exit_code = child.process->poll_exit();
            }
            // The result waits for the last line: a program that exited may still have output in the pipe.
            const bool closed = child.state->deliver_output();
            if (closed && (cancelled || child.exit_code.has_value())) {
                child.process->join();
                if (!cancelled) {
                    child.state->result = ProcessExit{.code = *child.exit_code};
                }
                child.done = true;
            }
        }
        std::erase_if(running, [](const Running& child) { return child.done; });
    }

    void end_running() {
        for (Running& child : running) {
            child.state->cancel();
            child.process->end();
        }
        for (Running& child : running) {
            child.process->join();
        }
        running.clear();
    }
#endif
};

ProcessLauncher::ProcessLauncher()
    : impl_(std::make_unique<Impl>()) {}

ProcessLauncher::~ProcessLauncher() {
    dispose();
}

void ProcessLauncher::dispose() {
    for (const Impl::Failed& entry : impl_->failed) {
        entry.state->cancel();
    }
    impl_->failed.clear();
#if defined(ENGINE_PROCESS_NATIVE)
    impl_->end_running();
#endif
}

ProcessCall ProcessLauncher::run(ProcessDesc desc) {
    auto state = std::make_shared<ProcessCallState>();
#if defined(ENGINE_PROCESS_NATIVE)
    if (auto started = NativeProcess::start(desc, state)) {
        impl_->running.push_back(Impl::Running{.state = state, .process = std::move(*started)});
    } else {
        impl_->failed.push_back(Impl::Failed{.state = state, .error = started.error()});
    }
#else
    (void)desc;
    impl_->failed.push_back(Impl::Failed{.state = state, .error = ProcessError::Unsupported});
#endif
    return ProcessCall{std::move(state)};
}

std::expected<void, ProcessError> ProcessLauncher::launch(const ProcessDesc& desc) {
#if defined(_WIN32)
    return launch_windows_process(desc);
#elif defined(ENGINE_PROCESS_POSIX)
    return launch_posix_process(desc);
#else
    (void)desc;
    return std::unexpected(ProcessError::Unsupported);
#endif
}

bool ProcessLauncher::is_supported() const {
#if defined(ENGINE_PROCESS_NATIVE)
    return true;
#else
    return false;
#endif
}

void ProcessLauncher::poll() {
    for (Impl::Failed& entry : impl_->failed) {
        if (!entry.state->cancelled()) {
            entry.state->result = std::unexpected(entry.error);
        }
    }
    impl_->failed.clear();
#if defined(ENGINE_PROCESS_NATIVE)
    impl_->poll_running();
#endif
}

}
