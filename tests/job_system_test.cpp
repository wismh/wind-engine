#include <gtest/gtest.h>

#include <engine/core/job_system.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> ranges_of(
        engine::JobSystem& jobs, std::size_t count, std::size_t grain) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    jobs.parallel_for(count, grain, [&](std::size_t begin, std::size_t end) { ranges.emplace_back(begin, end); });
    return ranges;
}

void expect_each_index_once(engine::JobSystem& jobs, std::size_t count, std::size_t grain) {
    std::vector<std::atomic<int>> hits(count);
    for (std::atomic<int>& hit : hits) {
        hit.store(0);
    }
    jobs.parallel_for(count, grain, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            hits[i].fetch_add(1);
        }
    });
    for (const std::atomic<int>& hit : hits) {
        EXPECT_EQ(hit.load(), 1);
    }
}

}

TEST(JobSystem, ZeroWorkersRunsRangesOnTheCaller) {
    engine::JobSystem jobs(0);
    EXPECT_EQ(jobs.worker_count(), 0);

    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id seen;
    jobs.parallel_for(4, 1, [&](std::size_t, std::size_t) { seen = std::this_thread::get_id(); });
    EXPECT_EQ(seen, caller);

    const std::vector<std::pair<std::size_t, std::size_t>> ranges = ranges_of(jobs, 10, 3);
    ASSERT_EQ(ranges.size(), 4u);
    EXPECT_EQ(ranges[0].first, 0u);
    EXPECT_EQ(ranges[0].second, 3u);
    EXPECT_EQ(ranges[1].first, 3u);
    EXPECT_EQ(ranges[1].second, 6u);
    EXPECT_EQ(ranges[2].first, 6u);
    EXPECT_EQ(ranges[2].second, 9u);
    EXPECT_EQ(ranges[3].first, 9u);
    EXPECT_EQ(ranges[3].second, 10u);
}

TEST(JobSystem, GrainZeroIsOneRange) {
    engine::JobSystem jobs(0);
    const std::vector<std::pair<std::size_t, std::size_t>> ranges = ranges_of(jobs, 5, 0);
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].first, 0u);
    EXPECT_EQ(ranges[0].second, 5u);
}

TEST(JobSystem, EmptyCountDoesNotCall) {
    engine::JobSystem jobs(0);
    int calls = 0;
    jobs.parallel_for(0, 1, [&](std::size_t, std::size_t) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(JobSystem, TwoWorkersVisitEachIndexOnce) {
    engine::JobSystem jobs(2);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    EXPECT_EQ(jobs.worker_count(), 2);
#endif
    for (int pass = 0; pass < 8; ++pass) {
        expect_each_index_once(jobs, 100, 7);
    }
}

TEST(JobSystem, CallerRunsTheFirstRangeAndAWorkerRunsAnother) {
    engine::JobSystem jobs(2);
    if (jobs.worker_count() == 0) {
        GTEST_SKIP() << "this build has no worker threads";
    }

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<int> workers_in{0};
    std::atomic<bool> caller_ran_first{false};
    jobs.parallel_for(8, 1, [&](std::size_t begin, std::size_t) {
        if (std::this_thread::get_id() != caller) {
            workers_in.fetch_add(1);
            return;
        }
        if (begin == 0) {
            caller_ran_first.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (workers_in.load() == 0 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }
    });
    EXPECT_TRUE(caller_ran_first.load());
    EXPECT_GT(workers_in.load(), 0);
}

TEST(JobSystem, NestedParallelForRunsOnTheSameThread) {
    engine::JobSystem jobs(2);
    std::atomic<int> sum{0};
    std::atomic<int> nested_off_caller{0};
    jobs.parallel_for(4, 1, [&](std::size_t, std::size_t) {
        const std::thread::id outer = std::this_thread::get_id();
        jobs.parallel_for(5, 2, [&](std::size_t begin, std::size_t end) {
            sum.fetch_add(static_cast<int>(end - begin));
            if (std::this_thread::get_id() != outer) {
                nested_off_caller.fetch_add(1);
            }
        });
    });
    EXPECT_EQ(sum.load(), 20);
    EXPECT_EQ(nested_off_caller.load(), 0);
}

TEST(JobSystem, StopJoinsAndLaterWorkRunsOnTheCaller) {
    engine::JobSystem jobs(2);
    expect_each_index_once(jobs, 16, 1);
    jobs.stop();
    EXPECT_EQ(jobs.worker_count(), 0);

    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id seen;
    jobs.parallel_for(3, 1, [&](std::size_t, std::size_t) { seen = std::this_thread::get_id(); });
    EXPECT_EQ(seen, caller);
    jobs.stop();
    EXPECT_EQ(jobs.worker_count(), 0);
}

TEST(JobSystem, DestructorJoinsWorkers) {
    std::atomic<int> hits{0};
    {
        engine::JobSystem jobs(2);
        jobs.parallel_for(4, 1, [&](std::size_t begin, std::size_t end) {
            hits.fetch_add(static_cast<int>(end - begin));
        });
        EXPECT_EQ(hits.load(), 4);
    }
    EXPECT_EQ(hits.load(), 4);
}
