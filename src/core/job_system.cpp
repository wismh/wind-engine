#include <engine/core/job_system.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
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

    // One `parallel_for`. Workers and the caller claim ranges with `next`. `left` hits zero only after
    // every claimed range has returned, which is what the caller waits for. The shared_ptr keeps the
    // batch alive while a worker is still leaving `participate` after that wait.
    struct Batch {
        std::vector<Range> ranges;
        void* context = nullptr;
        RangeFn fn = nullptr;
        std::atomic<std::size_t> next{0};
        std::atomic<std::size_t> left{0};
        std::mutex done_mutex;
        std::condition_variable done;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::shared_ptr<Batch> batch;
    std::uint64_t published = 0;
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
        batch.reset();
        stopping = false;
    }

    static void participate(const std::shared_ptr<Batch>& job) {
        for (;;) {
            const std::size_t index = job->next.fetch_add(1, std::memory_order_relaxed);
            if (index >= job->ranges.size()) {
                return;
            }
            const Range range = job->ranges[index];
            {
                const DepthGuard guard;
                job->fn(job->context, range.begin, range.end);
            }
            if (job->left.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                const std::lock_guard lock(job->done_mutex);
                job->done.notify_one();
            }
        }
    }

    void run() {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stopping || published != seen; });
            if (published == seen) {
                return;
            }
            seen = published;
            const std::shared_ptr<Batch> job = batch;
            lock.unlock();
            if (job) {
                participate(job);
            }
            lock.lock();
        }
    }
};

JobSystem::JobSystem(int workers) : impl_(std::make_unique<Impl>()) {
    const int count = resolve_workers(workers);
    impl_->workers = count;
    impl_->threads.reserve(static_cast<std::size_t>(count));
    // The worker holds `Impl` itself, not `impl_`: libc++ nulls the unique_ptr before it runs `~Impl`, so a worker
    // that starts late would read null while the destructor joins.
    Impl* const impl = impl_.get();
    for (int i = 0; i < count; ++i) {
        impl->threads.emplace_back([impl] {
            g_job_depth = 0;
            impl->run();
        });
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

    auto run_here = [&] {
        for (std::size_t begin = 0; begin < count; begin += span) {
            const DepthGuard guard;
            fn(context, begin, std::min(begin + span, count));
        }
    };

    if (impl_->workers == 0 || g_job_depth > 0) {
        run_here();
        return;
    }

    auto job = std::make_shared<Impl::Batch>();
    for (std::size_t begin = 0; begin < count; begin += span) {
        job->ranges.push_back(Impl::Range{.begin = begin, .end = std::min(begin + span, count)});
    }
    if (job->ranges.size() <= 1) {
        run_here();
        return;
    }
    job->context = context;
    job->fn = fn;
    // The caller owns the first range. Workers claim the rest.
    job->next.store(1, std::memory_order_relaxed);
    job->left.store(job->ranges.size(), std::memory_order_relaxed);

    {
        const std::lock_guard lock(impl_->mutex);
        impl_->batch = job;
        ++impl_->published;
    }
    impl_->wake.notify_all();

    {
        const DepthGuard guard;
        fn(context, job->ranges[0].begin, job->ranges[0].end);
    }
    if (job->left.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        const std::lock_guard lock(job->done_mutex);
        job->done.notify_one();
    }
    Impl::participate(job);
    {
        std::unique_lock lock(job->done_mutex);
        job->done.wait(lock, [&] { return job->left.load(std::memory_order_acquire) == 0; });
    }
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->batch == job) {
            impl_->batch.reset();
        }
    }
}

}
