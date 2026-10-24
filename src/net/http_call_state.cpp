#include "net/http_call_state.h"

#include <algorithm>

namespace engine {

void HttpCallState::cancel() {
    cancelled_.store(true, std::memory_order_release);
    {
        const std::scoped_lock lock(mutex_);
        if (abort_) {
            const std::function<void()> abort = std::move(abort_);
            abort_ = {};
            aborted_ = true;
            abort();
        }
    }
    std::unique_lock lock(sink_mutex_);
    sink_changed_.notify_all();
    sink_changed_.wait(lock, [this] { return !sink_running_; });
}

void HttpCallState::set_sink(HttpBodySink sink, SinkRoute route) {
    sink_ = std::move(sink);
    route_ = route;
}

std::optional<HttpError> HttpCallState::accept(int status, std::span<const std::byte> chunk, std::string& body) {
    if (cancelled()) {
        return HttpError::Network;
    }
    received_.fetch_add(chunk.size(), std::memory_order_acq_rel);
    if (sink_ && status >= 200 && status < 300) {
        if (deliver(chunk)) {
            return std::nullopt;
        }
        return sink_failed() ? HttpError::SinkAborted : HttpError::Network;
    }
    if (body.size() + chunk.size() > kHttpMaxResponseBytes) {
        return HttpError::TooLarge;
    }
    body.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    return std::nullopt;
}

bool HttpCallState::deliver(std::span<const std::byte> chunk) {
    switch (route_) {
    case SinkRoute::Inline:
        if (!sink_(chunk)) {
            sink_failed_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    case SinkRoute::Worker: {
        {
            const std::scoped_lock lock(sink_mutex_);
            if (cancelled()) {
                return false;
            }
            sink_running_ = true;
        }
        const bool keep_going = sink_(chunk);
        if (!keep_going) {
            sink_failed_.store(true, std::memory_order_release);
        }
        {
            const std::scoped_lock lock(sink_mutex_);
            sink_running_ = false;
        }
        sink_changed_.notify_all();
        return keep_going;
    }
    case SinkRoute::Queued: {
        std::unique_lock lock(sink_mutex_);
        sink_changed_.wait(lock, [this] { return queued_bytes_ < kSinkQueueBytes || cancelled() || sink_failed(); });
        if (cancelled() || sink_failed()) {
            return false;
        }
        queue_.emplace_back(chunk.begin(), chunk.end());
        queued_bytes_ += chunk.size();
        return true;
    }
    }
    return false;
}

void HttpCallState::drain_sink() {
    if (route_ != SinkRoute::Queued) {
        return;
    }
    std::vector<std::vector<std::byte>> chunks;
    {
        const std::scoped_lock lock(sink_mutex_);
        if (queue_.empty()) {
            return;
        }
        chunks.swap(queue_);
        queued_bytes_ = 0;
        draining_ = true;
    }
    // The transfer can fill the queue again while the sink writes.
    sink_changed_.notify_all();
    for (const std::vector<std::byte>& chunk : chunks) {
        if (cancelled() || sink_failed()) {
            break;
        }
        if (!sink_(chunk)) {
            sink_failed_.store(true, std::memory_order_release);
        }
    }
    {
        const std::scoped_lock lock(sink_mutex_);
        draining_ = false;
    }
    sink_changed_.notify_all();
}

bool HttpCallState::finish_sink() {
    if (route_ == SinkRoute::Queued) {
        std::unique_lock lock(sink_mutex_);
        sink_changed_.wait(
                lock, [this] { return (queue_.empty() && !draining_) || cancelled() || sink_failed(); });
    }
    return !sink_failed() && !cancelled();
}

}
