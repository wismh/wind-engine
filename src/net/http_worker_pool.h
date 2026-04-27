#pragma once

#include "net/http_completions.h"

#include <engine/net/http_request.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

// Runs blocking transfers (WinHTTP, HttpURLConnection) off the main thread. A call cancelled while queued
// never starts. A result of a call cancelled while running is dropped.
class HttpWorkerPool {
public:
    // Runs on a worker thread. Brackets its blocking work with HttpCallState::begin_transfer /
    // end_transfer so a cancel can abort it.
    using Transfer = std::function<HttpResult(HttpCallState& state, const HttpRequest& request)>;

    HttpWorkerPool(Transfer transfer, HttpCompletions& completions, int threads);
    HttpWorkerPool(const HttpWorkerPool&) = delete;
    HttpWorkerPool& operator=(const HttpWorkerPool&) = delete;
    ~HttpWorkerPool();

    void submit(std::shared_ptr<HttpCallState> state, HttpRequest request);

    // Drops queued jobs and joins the threads. Running transfers finish first: cancel their calls before.
    void stop();

private:
    struct Job {
        std::shared_ptr<HttpCallState> state;
        HttpRequest request;
    };

    void run();

    Transfer transfer_;
    HttpCompletions* completions_ = nullptr;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

}
