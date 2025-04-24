#include "net/http_worker_pool.h"

#include "net/http_call_state.h"
#include "net/http_completions.h"

#include <utility>

namespace engine {

HttpWorkerPool::HttpWorkerPool(Transfer transfer, HttpCompletions& completions, int threads)
    : transfer_(std::move(transfer))
    , completions_(&completions) {
    for (int i = 0; i < threads; ++i) {
        threads_.emplace_back([this] { run(); });
    }
}

HttpWorkerPool::~HttpWorkerPool() {
    stop();
}

void HttpWorkerPool::submit(std::shared_ptr<HttpCallState> state, HttpRequest request) {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        jobs_.push_back(Job{.state = std::move(state), .request = std::move(request)});
    }
    wake_.notify_one();
}

void HttpWorkerPool::stop() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void HttpWorkerPool::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (job.state->cancelled()) {
            continue;
        }
        HttpResult result = transfer_(*job.state, job.request);
        if (!job.state->cancelled()) {
            completions_->push(std::move(job.state), std::move(result));
        }
    }
}

}
