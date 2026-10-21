#include <engine/core/job_system.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace engine {
namespace {

thread_local int g_job_depth = 0;

struct DepthGuard {
    DepthGuard() {
        ++g_job_depth;
    }
    ~DepthGuard() {
        --g_job_depth;
    }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
};

[[nodiscard]] int resolve_workers(int requested) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    (void)requested;
    return 0;
#else
    if (requested == 0) {
        return 0;
    }
    if (requested > 0) {
        return requested;
    }
    const unsigned int cores = std::thread::hardware_concurrency();
    if (cores <= 1) {
        return 0;
    }
    return static_cast<int>(cores - 1);
#endif
}

}

struct JobSystem::Impl {
    struct Range {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable done;
    std::deque<Range> queue;
    std::size_t pending = 0;
    void* context = nullptr;
    RangeFn fn = nullptr;
    bool stopping = false;
    int workers = 0;
    std::vector<std::thread> threads;

    ~Impl() {
        stop();
    }

    void stop() {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();
        workers = 0;
        stopping = false;
    }

    void run() {
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stopping || !queue.empty(); });
            if (queue.empty()) {
                return;
            }
            const Range range = queue.front();
            queue.pop_front();
            void* call_context = context;
            const RangeFn call = fn;
            lock.unlock();
            {
                const DepthGuard guard;
                call(call_context, range.begin, range.end);
            }
            lock.lock();
            --pending;
            if (pending == 0) {
                done.notify_one();
            }
        }
    }
};

JobSystem::JobSystem(int workers) : impl_(std::make_unique<Impl>()) {
    const int count = resolve_workers(workers);
    impl_->workers = count;
    impl_->threads.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        impl_->threads.emplace_back([this] { impl_->run(); });
    }
}

JobSystem::~JobSystem() = default;

void JobSystem::stop() {
    impl_->stop();
}

int JobSystem::worker_count() const noexcept {
    return impl_->workers;
}

void JobSystem::parallel_for_impl(std::size_t count, std::size_t grain, void* context, RangeFn fn) {
    if (count == 0) {
        return;
    }
    const std::size_t span = (grain == 0 || grain > count) ? count : grain;

    auto call = [&](std::size_t begin, std::size_t end) {
        const DepthGuard guard;
        fn(context, begin, end);
    };
    auto run_here = [&] {
        for (std::size_t begin = 0; begin < count; begin += span) {
            call(begin, std::min(begin + span, count));
        }
    };

    if (impl_->workers == 0 || g_job_depth > 0) {
        run_here();
        return;
    }

    std::vector<Impl::Range> ranges;
    for (std::size_t begin = 0; begin < count; begin += span) {
        ranges.push_back(Impl::Range{.begin = begin, .end = std::min(begin + span, count)});
    }
    if (ranges.size() <= 1) {
        run_here();
        return;
    }

    {
        const std::lock_guard lock(impl_->mutex);
        impl_->context = context;
        impl_->fn = fn;
        impl_->pending = ranges.size();
        impl_->queue.clear();
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            impl_->queue.push_back(ranges[i]);
        }
    }
    impl_->wake.notify_all();

    call(ranges[0].begin, ranges[0].end);
    {
        const std::lock_guard lock(impl_->mutex);
        --impl_->pending;
        if (impl_->pending == 0) {
            impl_->done.notify_one();
        }
    }

    for (;;) {
        Impl::Range range;
        {
            const std::lock_guard lock(impl_->mutex);
            if (impl_->queue.empty()) {
                break;
            }
            range = impl_->queue.front();
            impl_->queue.pop_front();
        }
        call(range.begin, range.end);
        const std::lock_guard lock(impl_->mutex);
        --impl_->pending;
        if (impl_->pending == 0) {
            impl_->done.notify_one();
        }
    }

    std::unique_lock lock(impl_->mutex);
    impl_->done.wait(lock, [&] { return impl_->pending == 0; });
    impl_->context = nullptr;
    impl_->fn = nullptr;
}

}
