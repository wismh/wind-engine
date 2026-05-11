#include <engine/process/process_launcher.h>

#include "process/process_call_state.h"

#if defined(_WIN32)
#include "process/windows_process.h"
#endif

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace engine {

struct ProcessLauncher::Impl {
    // Calls whose program never started. Their error reaches them on the next poll.
    struct Failed {
        std::shared_ptr<ProcessCallState> state;
        ProcessError error;
    };
    std::vector<Failed> failed;

#if defined(_WIN32)
    struct Running {
        std::shared_ptr<ProcessCallState> state;
        std::unique_ptr<WindowsProcess> process;
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
#if defined(_WIN32)
    impl_->end_running();
#endif
}

ProcessCall ProcessLauncher::run(ProcessDesc desc) {
    auto state = std::make_shared<ProcessCallState>();
#if defined(_WIN32)
    if (auto started = WindowsProcess::start(desc, state)) {
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
#else
    (void)desc;
    return std::unexpected(ProcessError::Unsupported);
#endif
}

bool ProcessLauncher::is_supported() const {
#if defined(_WIN32)
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
#if defined(_WIN32)
    impl_->poll_running();
#endif
}

}
