#include <engine/core/job_system.h>

#include "core/job_state.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
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

// A slot taken out of a job under the lock, destroyed after it.
struct DoomedSlot {
    void* slot = nullptr;
    detail::JobDestroyFn destroy = nullptr;
};

// Takes the slot out of a job that leaves the system cancelled.
[[nodiscard]] DoomedSlot drop(JobState& state) {
    state.cancelled.store(true, std::memory_order_release);
    const DoomedSlot doomed{.slot = state.slot, .destroy = state.destroy};
    state.slot = nullptr;
    state.status = JobStatus::Dropped;
    state.owner.store(nullptr, std::memory_order_release);
    return doomed;
}

void destroy(const DoomedSlot& doomed) {
    if (doomed.slot != nullptr) {
        doomed.destroy(doomed.slot);
    }
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

    // Highest priority first, then submit order. `state` does not take part in the order, so a key with a null
    // state finds an entry.
    struct Queued {
        int priority = 0;
        std::uint64_t seq = 0;
        std::shared_ptr<JobState> state;

        bool operator<(const Queued& other) const {
            if (priority != other.priority) {
                return priority > other.priority;
            }
            return seq < other.seq;
        }
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::shared_ptr<Batch> batch;
    std::uint64_t published = 0;
    bool stopping = false;
    int workers = 0;
    std::vector<std::thread> threads;
    std::thread::id owner_thread = std::this_thread::get_id();

    // Jobs, all under `mutex`. They live in their own list rather than a CallCompletions: a cancel must see in
    // one critical section whether a job is queued, running, or finished, and `cancel_all` must reach the
    // finished ones that `poll` has not delivered yet.
    //
    // `busy` counts jobs from pop until they are in `finished` or their slot has been destroyed; `idle` is
    // signalled when it drops.
    std::set<Queued> queue;
    std::vector<std::shared_ptr<JobState>> running;
    std::vector<std::shared_ptr<JobState>> finished;
    std::uint64_t next_seq = 0;
    int busy = 0;
    std::condition_variable idle;

    ~Impl() {
        stop();
    }

    // At most `workers - 1` jobs at once, so a `parallel_for` always finds a free worker. One worker runs them all.
    [[nodiscard]] int job_cap() const {
        return workers >= 2 ? workers - 1 : workers;
    }

    [[nodiscard]] bool can_take_job() const {
        return !queue.empty() && busy < job_cap();
    }

    void stop() {
        cancel_all();
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

    // Takes the top job and runs it on this thread. Called with `lock` held and a non-empty queue; returns with
    // it held.
    void run_one(std::unique_lock<std::mutex>& lock) {
        const auto top = queue.begin();
        std::shared_ptr<JobState> state = top->state;
        queue.erase(top);
        state->status = JobStatus::Running;
        running.push_back(state);
        ++busy;
        lock.unlock();

        {
            const DepthGuard guard;
            const JobToken token{state->cancelled};
            state->run(state->slot, token);
        }

        lock.lock();
        std::erase(running, state);
        if (state->cancelled.load(std::memory_order_acquire)) {
            const DoomedSlot doomed = drop(*state);
            // The slot's destructor is code of whatever module submitted the job. `busy` stays up until it has
            // returned, so `cancel_all` does not let that module go while it runs.
            lock.unlock();
            destroy(doomed);
            lock.lock();
        } else {
            state->status = JobStatus::Finished;
            finished.push_back(std::move(state));
        }
        --busy;
        idle.notify_all();
        wake.notify_one();
    }

    void cancel_all() {
        std::vector<DoomedSlot> doomed;
        std::unique_lock lock(mutex);
        doomed.reserve(queue.size() + finished.size());
        for (const Queued& entry : queue) {
            doomed.push_back(drop(*entry.state));
        }
        queue.clear();
        for (const std::shared_ptr<JobState>& state : running) {
            state->cancelled.store(true, std::memory_order_release);
        }
        for (const std::shared_ptr<JobState>& state : finished) {
            doomed.push_back(drop(*state));
        }
        finished.clear();
        lock.unlock();
        for (const DoomedSlot& entry : doomed) {
            destroy(entry);
        }
        lock.lock();
        idle.wait(lock, [this] { return busy == 0; });
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

    // A published batch comes before a queued job.
    void run() {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stopping || published != seen || can_take_job(); });
            if (published != seen) {
                seen = published;
                const std::shared_ptr<Batch> job = batch;
                lock.unlock();
                if (job) {
                    participate(job);
                }
                lock.lock();
                continue;
            }
            if (stopping) {
                return;
            }
            run_one(lock);
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
    check_owner_thread();
    impl_->stop();
}

int JobSystem::worker_count() const noexcept {
    return impl_->workers;
}

void JobSystem::check_owner_thread() const {
    assert(std::this_thread::get_id() == impl_->owner_thread && "JobSystem: call it from the thread that built it");
}

JobHandle JobSystem::submit_impl(void* slot, detail::JobRunFn run, detail::JobDestroyFn destroy, int priority) {
    check_owner_thread();
    auto state = std::make_shared<JobState>();
    state->slot = slot;
    state->run = run;
    state->destroy = destroy;
    state->priority = priority;
    state->owner.store(this, std::memory_order_release);
    {
        const std::lock_guard lock(impl_->mutex);
        state->seq = impl_->next_seq++;
        impl_->queue.insert(Impl::Queued{.priority = priority, .seq = state->seq, .state = state});
    }
    impl_->wake.notify_one();
    return JobHandle{std::move(state)};
}

void JobSystem::poll(std::chrono::microseconds inline_budget) {
    check_owner_thread();
    std::unique_lock lock(impl_->mutex);
    if (impl_->workers == 0 && !impl_->queue.empty()) {
        const auto start = std::chrono::steady_clock::now();
        do {
            impl_->run_one(lock);
        } while (!impl_->queue.empty() && std::chrono::steady_clock::now() - start < inline_budget);
    }
    std::vector<std::shared_ptr<JobState>> done;
    done.swap(impl_->finished);
    for (const std::shared_ptr<JobState>& state : done) {
        state->status = JobStatus::Delivered;
        state->owner.store(nullptr, std::memory_order_release);
    }
    lock.unlock();
    for (const std::shared_ptr<JobState>& state : done) {
        state->delivered = true;
    }
}

void JobSystem::wait_idle() {
    check_owner_thread();
    std::unique_lock lock(impl_->mutex);
    if (impl_->workers == 0) {
        while (!impl_->queue.empty()) {
            impl_->run_one(lock);
        }
        return;
    }
    impl_->idle.wait(lock, [this] { return impl_->queue.empty() && impl_->busy == 0; });
}

void JobSystem::cancel_all() {
    check_owner_thread();
    impl_->cancel_all();
}

void JobSystem::cancel_job(JobState& state) {
    check_owner_thread();
    DoomedSlot doomed;
    {
        const std::lock_guard lock(impl_->mutex);
        switch (state.status) {
        case JobStatus::Queued:
            impl_->queue.erase(Impl::Queued{.priority = state.priority, .seq = state.seq, .state = nullptr});
            break;
        case JobStatus::Finished:
            std::erase_if(impl_->finished,
                    [&state](const std::shared_ptr<JobState>& entry) { return entry.get() == &state; });
            break;
        case JobStatus::Running:
            // The worker sees the flag when the function returns and destroys the slot there.
            state.cancelled.store(true, std::memory_order_release);
            return;
        case JobStatus::Delivered:
        case JobStatus::Dropped:
            return;
        }
        doomed = drop(state);
    }
    destroy(doomed);
}

void JobSystem::reprioritize_job(JobState& state, int priority) {
    check_owner_thread();
    const std::lock_guard lock(impl_->mutex);
    if (state.status != JobStatus::Queued) {
        return;
    }
    const auto found = impl_->queue.find(Impl::Queued{.priority = state.priority, .seq = state.seq, .state = nullptr});
    if (found == impl_->queue.end()) {
        return;
    }
    std::shared_ptr<JobState> shared = found->state;
    impl_->queue.erase(found);
    state.priority = priority;
    impl_->queue.insert(Impl::Queued{.priority = priority, .seq = state.seq, .state = std::move(shared)});
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
    job->ranges.reserve((count + span - 1) / span);
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
