#include <gtest/gtest.h>

#include <engine/core/job_system.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

// Holds a worker inside a job until the test lets it go. No sleeps: both sides wait on atomics.
struct Gate {
    std::atomic<bool> started{false};
    std::atomic<bool> open{false};

    void enter() {
        started.store(true);
        started.notify_all();
        open.wait(false);
    }
    void wait_started() {
        started.wait(false);
    }
    void release() {
        open.store(true);
        open.notify_all();
    }
};

// Jobs append their name here, from any thread.
struct Order {
    std::mutex mutex;
    std::vector<int> seen;

    void add(int value) {
        const std::scoped_lock lock(mutex);
        seen.push_back(value);
    }
};

#define SKIP_WITHOUT_WORKERS(jobs)                                                                                     \
    if ((jobs).worker_count() == 0) {                                                                                  \
        GTEST_SKIP() << "no worker threads on this platform";                                                          \
    }

}

TEST(JobCall, ResultArrivesOnlyOnPoll) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    engine::JobCall<int> call = jobs.submit([] { return 42; });
    jobs.wait_idle();

    EXPECT_TRUE(call.pending()) << "the job finished, but only poll hands the result over";
    EXPECT_EQ(call.take(), std::nullopt);

    jobs.poll();

    EXPECT_FALSE(call.pending());
    EXPECT_EQ(call.take(), 42);
    EXPECT_EQ(call.take(), std::nullopt) << "the result is taken once";
}

TEST(JobCall, ZeroWorkersRunJobsInPollOnTheCaller) {
    engine::JobSystem jobs(0);
    std::atomic<bool> ran{false};
    std::thread::id thread;
    engine::JobCall<int> call = jobs.submit([&] {
        ran.store(true);
        thread = std::this_thread::get_id();
        return 7;
    });
    EXPECT_FALSE(ran.load()) << "submit does not run the job";
    EXPECT_TRUE(call.pending());

    jobs.poll();

    EXPECT_TRUE(ran.load());
    EXPECT_EQ(thread, std::this_thread::get_id());
    EXPECT_EQ(call.take(), 7) << "a job run inline is delivered by the same poll";
}

TEST(JobCall, ZeroBudgetPollRunsOneJobHighestPriorityFirst) {
    engine::JobSystem jobs(0);
    Order order;
    engine::JobCall<int> a = jobs.submit([&] { order.add(1); return 1; }, 0);
    engine::JobCall<int> b = jobs.submit([&] { order.add(2); return 2; }, 5);
    engine::JobCall<int> c = jobs.submit([&] { order.add(3); return 3; }, 5);
    engine::JobCall<int> d = jobs.submit([&] { order.add(4); return 4; }, -1);
    d.set_priority(10);

    jobs.poll(std::chrono::microseconds{0});
    EXPECT_EQ(order.seen, (std::vector<int>{4}));
    EXPECT_EQ(d.take(), 4);
    EXPECT_TRUE(b.pending());

    jobs.poll(std::chrono::microseconds{0});
    jobs.poll(std::chrono::microseconds{0});
    jobs.poll(std::chrono::microseconds{0});
    EXPECT_EQ(order.seen, (std::vector<int>{4, 2, 3, 1})) << "higher first, submit order among equals";
    EXPECT_EQ(a.take(), 1);
    EXPECT_EQ(b.take(), 2);
    EXPECT_EQ(c.take(), 3);
}

TEST(JobCall, OneWorkerRunsQueuedJobsByPriority) {
    engine::JobSystem jobs(1);
    SKIP_WITHOUT_WORKERS(jobs);
    Gate gate;
    Order order;
    engine::JobCall<int> blocker = jobs.submit([&] {
        gate.enter();
        return 0;
    });
    gate.wait_started();
    engine::JobCall<int> a = jobs.submit([&] { order.add(1); return 1; }, 0);
    engine::JobCall<int> b = jobs.submit([&] { order.add(2); return 2; }, 5);
    engine::JobCall<int> c = jobs.submit([&] { order.add(3); return 3; }, 5);
    engine::JobCall<int> d = jobs.submit([&] { order.add(4); return 4; }, -1);
    d.set_priority(10);

    gate.release();
    jobs.wait_idle();

    EXPECT_EQ(order.seen, (std::vector<int>{4, 2, 3, 1}));
}

TEST(JobCall, CancelBeforeRunNeverRuns) {
    engine::JobSystem jobs(1);
    SKIP_WITHOUT_WORKERS(jobs);
    Gate gate;
    engine::JobCall<int> blocker = jobs.submit([&] {
        gate.enter();
        return 0;
    });
    gate.wait_started();
    const auto sentinel = std::make_shared<int>(0);
    std::atomic<bool> ran{false};
    engine::JobCall<int> call = jobs.submit([&ran, sentinel] {
        ran.store(true);
        return *sentinel;
    });
    ASSERT_EQ(sentinel.use_count(), 2);

    call.cancel();

    EXPECT_EQ(sentinel.use_count(), 1) << "a queued job's function is destroyed at cancel";
    EXPECT_FALSE(call.pending());
    gate.release();
    jobs.wait_idle();
    jobs.poll();
    EXPECT_FALSE(ran.load());
    EXPECT_EQ(call.take(), std::nullopt);
    EXPECT_EQ(blocker.take(), 0);
}

TEST(JobCall, CancelWhileRunningDiscardsTheResultAndTheTokenSeesIt) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    std::atomic<bool> started{false};
    std::atomic<bool> saw_cancel{false};
    const auto sentinel = std::make_shared<int>(5);
    engine::JobCall<int> call = jobs.submit([&, sentinel](const engine::JobToken& token) {
        started.store(true);
        started.notify_all();
        while (!token.cancelled()) {
            std::this_thread::yield();
        }
        saw_cancel.store(true);
        return *sentinel;
    });
    started.wait(false);

    call.cancel();
    EXPECT_FALSE(call.pending());
    jobs.wait_idle();

    EXPECT_TRUE(saw_cancel.load());
    EXPECT_EQ(sentinel.use_count(), 1) << "the worker destroys a cancelled job's function";
    jobs.poll();
    EXPECT_EQ(call.take(), std::nullopt);
}

TEST(JobCall, DroppingACallCancelsIt) {
    engine::JobSystem jobs(0);
    const auto queued = std::make_shared<int>(1);
    std::atomic<bool> ran{false};
    {
        engine::JobCall<int> call = jobs.submit([&ran, queued] {
            ran.store(true);
            return *queued;
        });
    }
    EXPECT_EQ(queued.use_count(), 1);

    const auto replaced = std::make_shared<int>(2);
    engine::JobCall<int> call = jobs.submit([&ran, replaced] {
        ran.store(true);
        return *replaced;
    });
    call = jobs.submit([] { return 3; });
    EXPECT_EQ(replaced.use_count(), 1) << "move-assigning over a pending call cancels it";

    jobs.poll();
    EXPECT_FALSE(ran.load());
    EXPECT_EQ(call.take(), 3);
}

TEST(JobCall, DroppingARunningCallCancelsIt) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    std::atomic<bool> started{false};
    std::atomic<bool> saw_cancel{false};
    const auto sentinel = std::make_shared<int>(0);
    {
        engine::JobCall<int> call = jobs.submit([&, sentinel](const engine::JobToken& token) {
            started.store(true);
            started.notify_all();
            while (!token.cancelled()) {
                std::this_thread::yield();
            }
            saw_cancel.store(true);
            return 0;
        });
        started.wait(false);
    }
    jobs.wait_idle();
    EXPECT_TRUE(saw_cancel.load());
    EXPECT_EQ(sentinel.use_count(), 1);
}

TEST(JobCall, CancelAfterFinishBeforePollDropsTheResult) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    const auto sentinel = std::make_shared<int>(0);
    engine::JobCall<std::shared_ptr<int>> call = jobs.submit([sentinel] { return sentinel; });
    jobs.wait_idle();
    ASSERT_EQ(sentinel.use_count(), 3) << "the function's copy and the result's";

    call.cancel();

    EXPECT_EQ(sentinel.use_count(), 1);
    jobs.poll();
    EXPECT_EQ(call.take(), std::nullopt);
}

TEST(JobCall, ParallelForCompletesWhileJobsHoldWorkers) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    Gate gate;
    std::vector<engine::JobCall<int>> blockers;
    blockers.push_back(jobs.submit([&] {
        gate.enter();
        return 0;
    }));
    blockers.push_back(jobs.submit([] { return 1; }));
    gate.wait_started();

    std::vector<std::atomic<int>> hits(100);
    for (int pass = 0; pass < 8; ++pass) {
        for (std::atomic<int>& hit : hits) {
            hit.store(0);
        }
        jobs.parallel_for(hits.size(), 7, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                hits[i].fetch_add(1);
            }
        });
        for (const std::atomic<int>& hit : hits) {
            EXPECT_EQ(hit.load(), 1);
        }
    }

    gate.release();
    jobs.wait_idle();
}

TEST(JobCall, ParallelForInsideAJobRunsOnThatWorker) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    engine::JobCall<bool> call = jobs.submit([&jobs] {
        const std::thread::id self = std::this_thread::get_id();
        std::atomic<bool> same{true};
        jobs.parallel_for(10, 1, [&](std::size_t, std::size_t) {
            if (std::this_thread::get_id() != self) {
                same.store(false);
            }
        });
        return same.load();
    });
    jobs.wait_idle();
    jobs.poll();
    EXPECT_EQ(call.take(), true);
}

TEST(JobCall, ManyJobsOnManyWorkers) {
    engine::JobSystem jobs(4);
    SKIP_WITHOUT_WORKERS(jobs);
    std::vector<engine::JobCall<std::unique_ptr<std::size_t>>> calls;
    for (std::size_t i = 0; i < 1000; ++i) {
        calls.push_back(jobs.submit([i] { return std::make_unique<std::size_t>(i * i); }, static_cast<int>(i % 7)));
    }
    jobs.wait_idle();
    jobs.poll();
    for (std::size_t i = 0; i < calls.size(); ++i) {
        std::optional<std::unique_ptr<std::size_t>> value = calls[i].take();
        ASSERT_TRUE(value.has_value()) << i;
        EXPECT_EQ(**value, i * i);
    }
}

TEST(JobCall, CancelAllWaitsForRunningJobsAndDropsTheRest) {
    engine::JobSystem jobs(2);
    SKIP_WITHOUT_WORKERS(jobs);
    const auto sentinel = std::make_shared<int>(0);
    engine::JobCall<int> done = jobs.submit([sentinel] { return 1; });
    jobs.wait_idle();

    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    engine::JobCall<int> running = jobs.submit([&, sentinel](const engine::JobToken& token) {
        started.store(true);
        started.notify_all();
        while (!token.cancelled()) {
            std::this_thread::yield();
        }
        finished.store(true);
        return 2;
    });
    started.wait(false);
    // Jobs use one of the two workers at a time, and that one is busy: this job stays queued.
    std::atomic<bool> queued_ran{false};
    engine::JobCall<int> queued = jobs.submit([&, sentinel] {
        queued_ran.store(true);
        return 3;
    });

    jobs.cancel_all();

    EXPECT_TRUE(finished.load()) << "cancel_all returns only after the running job has";
    EXPECT_FALSE(queued_ran.load());
    EXPECT_EQ(sentinel.use_count(), 1);
    EXPECT_FALSE(done.pending());
    EXPECT_FALSE(running.pending());
    EXPECT_FALSE(queued.pending());
    jobs.poll();
    EXPECT_EQ(done.take(), std::nullopt);
    EXPECT_EQ(running.take(), std::nullopt);
    EXPECT_EQ(queued.take(), std::nullopt);
}

TEST(JobCall, StopCancelsThenLaterJobsRunInPoll) {
    engine::JobSystem jobs(2);
    engine::JobCall<int> before = jobs.submit([] { return 1; });
    jobs.stop();
    EXPECT_FALSE(before.pending());
    EXPECT_EQ(jobs.worker_count(), 0);

    engine::JobCall<int> after = jobs.submit([] { return 2; });
    jobs.poll();
    EXPECT_EQ(after.take(), 2);
}

TEST(JobCall, CallsOutliveTheSystem) {
    engine::JobCall<int> queued;
    engine::JobCall<int> finished;
    {
        engine::JobSystem jobs(0);
        queued = jobs.submit([] { return 1; }, -1);
        finished = jobs.submit([] { return 2; });
        jobs.poll(std::chrono::microseconds{0});
        ASSERT_TRUE(finished.pending() == false);
    }
    EXPECT_FALSE(queued.pending());
    EXPECT_EQ(queued.take(), std::nullopt);
    EXPECT_EQ(finished.take(), 2) << "a delivered result belongs to its call";
    queued.cancel();
    queued.set_priority(3);
}

TEST(JobCall, ResolvedCallTakesOnce) {
    using Call = engine::JobCall<std::unique_ptr<int>>;
    Call call = Call::resolved(std::make_unique<int>(9));
    EXPECT_FALSE(call.pending());
    std::optional<std::unique_ptr<int>> value = call.take();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(**value, 9);
    EXPECT_EQ(call.take(), std::nullopt);
}

TEST(JobCall, EmptyCallIgnoresEverything) {
    engine::JobCall<int> call;
    EXPECT_FALSE(call.pending());
    EXPECT_EQ(call.take(), std::nullopt);
    call.set_priority(1);
    call.cancel();
}

TEST(JobCall, SetPriorityAfterTheJobRanIsIgnored) {
    engine::JobSystem jobs(0);
    engine::JobCall<int> call = jobs.submit([] { return 4; });
    jobs.wait_idle();
    call.set_priority(100);
    jobs.poll();
    call.set_priority(100);
    EXPECT_EQ(call.take(), 4);
}
