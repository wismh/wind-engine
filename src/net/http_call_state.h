#pragma once

#include <engine/net/http_request.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace engine {

// Shared by one HttpCall, the client's completion queue, and the backend running the transfer.
//
// `result` is main-thread only: CallCompletions::deliver writes it, HttpCall reads it.
// `cancel` runs on the main thread. A backend thread brackets its blocking work with
// `begin_transfer` / `end_transfer`, and `cancel` runs the abort it registered in between.
struct HttpCallState {
    std::optional<HttpResult> result;

    [[nodiscard]] bool cancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }

    // Registers how to abort the running transfer (close its handle). False if the call was cancelled
    // already: the backend must not start.
    [[nodiscard]] bool begin_transfer(std::function<void()> abort) {
        const std::scoped_lock lock(mutex_);
        if (cancelled()) {
            return false;
        }
        abort_ = std::move(abort);
        return true;
    }

    // Forgets the abort. True if `cancel` ran it, so the handle it closed must not be closed again.
    [[nodiscard]] bool end_transfer() {
        const std::scoped_lock lock(mutex_);
        abort_ = {};
        return aborted_;
    }

    // Marks the call cancelled and runs the registered abort, under the lock so it cannot race
    // `end_transfer`. The abort may complete the transfer synchronously (Web); that path must not lock.
    void cancel() {
        cancelled_.store(true, std::memory_order_release);
        const std::scoped_lock lock(mutex_);
        if (abort_) {
            const std::function<void()> abort = std::move(abort_);
            abort_ = {};
            aborted_ = true;
            abort();
        }
    }

private:
    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    std::function<void()> abort_;
    bool aborted_ = false;
};

}
