#pragma once

#include <engine/process/process_desc.h>

#include <atomic>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine {

// Shared by one ProcessCall, the launcher, and the thread reading the child's output.
//
// `output`, `result`, and `result_taken` are main-thread only: the launcher's poll writes them, ProcessCall reads
// them. The reader thread only pushes lines and closes the output; `deliver_output` moves them over on the main
// thread. `cancel` only marks the call: the launcher ends the processes in its next poll.
struct ProcessCallState {
    std::vector<std::string> output;
    std::optional<ProcessResult> result;
    bool result_taken = false;

    [[nodiscard]] bool cancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }

    void cancel() {
        cancelled_.store(true, std::memory_order_release);
    }

    // Reader thread: lines read since the last push.
    void push_lines(std::vector<std::string> lines) {
        const std::scoped_lock lock(mutex_);
        incoming_.insert(incoming_.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
    }

    // Reader thread: the pipe is closed, no line follows.
    void close_output() {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
    }

    // Main thread: appends the pushed lines to `output` (drops them once cancelled). True once the output closed and
    // every line was moved.
    [[nodiscard]] bool deliver_output() {
        std::vector<std::string> lines;
        bool closed = false;
        {
            const std::scoped_lock lock(mutex_);
            lines.swap(incoming_);
            closed = closed_;
        }
        if (!cancelled()) {
            output.insert(output.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
        }
        return closed;
    }

private:
    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    std::vector<std::string> incoming_;
    bool closed_ = false;
};

}
