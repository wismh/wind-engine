#pragma once

#include <engine/net/http_request.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine {

// Chunks a `Main` sink may have waiting before the download pauses.
inline constexpr std::size_t kSinkQueueBytes = 4u * 1024u * 1024u;

// Where the body sink of one call runs.
enum class SinkRoute {
    // The caller's thread, which is the main thread (Web). Nothing is shared.
    Inline,
    // The transfer thread. `cancel` waits for a running sink.
    Worker,
    // The main thread in `drain_sink`, through a bounded queue.
    Queued,
};

// Shared by one HttpCall, the client's completion queue, and the backend running the transfer.
//
// `result` is main-thread only: CallCompletions::deliver writes it, HttpCall reads it.
// `cancel` runs on the main thread. A backend thread brackets its blocking work with
// `begin_transfer` / `end_transfer`, and `cancel` runs the abort it registered in between.
//
// The body goes through `accept`, on the transfer thread. It counts progress, then hands a 2xx chunk to the
// sink (when the request had one) or appends it to the response body.
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
    // Then waits for a `Worker` sink that is running, so nothing the sink captured is used after this returns.
    void cancel();

    // Set once, before the transfer starts, from the request's sink.
    void set_sink(HttpBodySink sink, SinkRoute route);

    // One piece of the body, from the transfer thread. `status` is the response status. Returns nullopt to go
    // on, or the error that ends the transfer: Network when the call was cancelled, SinkAborted when the sink
    // said no, TooLarge for a body without a sink.
    [[nodiscard]] std::optional<HttpError> accept(int status, std::span<const std::byte> chunk, std::string& body);

    // Main thread, `Queued` route: hands the waiting chunks to the sink and lets the transfer go on.
    void drain_sink();

    // Transfer thread, after the last `accept`: waits until the main thread has taken every queued chunk.
    // False if the sink aborted, or the call was cancelled meanwhile.
    [[nodiscard]] bool finish_sink();

    [[nodiscard]] bool sink_failed() const {
        return sink_failed_.load(std::memory_order_acquire);
    }

    void set_total(std::uint64_t bytes) {
        total_.store(static_cast<std::int64_t>(bytes), std::memory_order_release);
    }
    // For a backend that learns the position from the platform (Web) instead of counting through `accept`.
    void set_received(std::uint64_t bytes) {
        received_.store(bytes, std::memory_order_release);
    }
    [[nodiscard]] std::uint64_t received() const {
        return received_.load(std::memory_order_acquire);
    }
    [[nodiscard]] HttpProgress progress() const {
        const std::int64_t total = total_.load(std::memory_order_acquire);
        return HttpProgress{
                .received = received(),
                .total = total >= 0 ? std::optional<std::uint64_t>{static_cast<std::uint64_t>(total)} : std::nullopt,
        };
    }

private:
    [[nodiscard]] bool deliver(std::span<const std::byte> chunk);

    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    std::function<void()> abort_;
    bool aborted_ = false;

    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::int64_t> total_{-1};

    HttpBodySink sink_;
    SinkRoute route_ = SinkRoute::Inline;
    std::atomic<bool> sink_failed_{false};
    // Guards everything below. `sink_changed_` wakes the transfer thread (queue room, cancel) and `cancel`
    // (the sink returned).
    std::mutex sink_mutex_;
    std::condition_variable sink_changed_;
    std::vector<std::vector<std::byte>> queue_;
    std::size_t queued_bytes_ = 0;
    bool draining_ = false;
    bool sink_running_ = false;
};

}
