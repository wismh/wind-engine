#pragma once

// docs/tech/modules/Core.md

#include <engine/core/export.h>
#include <engine/core/job_call.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace engine {

// How long `JobSystem::poll` runs queued jobs on the caller when there are no workers.
inline constexpr std::chrono::microseconds kJobInlineBudget{4000};

namespace detail {

template <typename Fn, bool Token = std::is_invocable_v<Fn&, const JobToken&>>
struct JobFnResult {
    using type = std::remove_cvref_t<std::invoke_result_t<Fn&>>;
};

template <typename Fn>
struct JobFnResult<Fn, true> {
    using type = std::remove_cvref_t<std::invoke_result_t<Fn&, const JobToken&>>;
};

template <typename Fn>
using JobFnResultT = typename JobFnResult<Fn>::type;

// The function and its result. The engine holds it as the `JobResult<T>` subobject behind a `void*`.
template <typename T, typename Fn>
struct JobBox final : JobResult<T> {
    template <typename F>
    explicit JobBox(F&& f) : fn(std::forward<F>(f)) {}

    Fn fn;

    static void run(void* slot, const JobToken& token) noexcept {
        auto* const box = static_cast<JobBox*>(static_cast<JobResult<T>*>(slot));
        if constexpr (std::is_invocable_v<Fn&, const JobToken&>) {
            box->value.emplace(box->fn(token));
        } else {
            box->value.emplace(box->fn());
        }
    }

    static void destroy(void* slot) noexcept {
        delete static_cast<JobBox*>(static_cast<JobResult<T>*>(slot));
    }
};

}

// Worker pool for two kinds of work, sharing the same threads:
//
// - `parallel_for` is fork-join. It is called from the thread that owns the system and returns only after every
//   index has been visited once. With no workers, and on Web without pthreads, the ranges run on the caller.
//   A `parallel_for` made from inside a running range or job also runs on that thread.
// - `submit` starts a background job and returns a `JobCall`. The result reaches the call in `poll`, on the main
//   thread. Jobs run on at most `workers - 1` workers at once (all of them with one worker), so fork-join always
//   has a free worker. With no workers, `poll` runs them on the caller.
//
// A function must not throw (a throw is `std::terminate`) and must not call engine APIs other than the any-thread
// `compress` and `decompress` (<engine/core/compression.h>). A `parallel_for` range may read what the caller shares
// and write disjoint indices of a buffer the caller owns. A job owns or shares everything it reads, because the owner
// of its call may be destroyed while it runs, and returns data: the main thread turns it into meshes or textures
// after `take`.
//
// `submit`, `poll`, `wait_idle`, `cancel_all`, `stop`, and every `JobCall` call run on the thread that built the
// system (checked in debug builds).
class JobSystem {
public:
    // `workers` < 0 is `hardware_concurrency - 1`, or 0 when the machine has one core (and always 0 on Web
    // without pthreads). `workers` == 0 runs every range on the caller and every job in `poll`.
    ENGINE_API explicit JobSystem(int workers = -1);
    ENGINE_API ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Half-open ranges of at most `grain` indices. `grain` == 0 is one range covering `count`.
    template <typename Fn>
    void parallel_for(std::size_t count, std::size_t grain, Fn&& fn) {
        auto* context = &fn;
        parallel_for_impl(count, grain, context, [](void* ctx, std::size_t begin, std::size_t end) {
            (*static_cast<std::remove_reference_t<Fn>*>(ctx))(begin, end);
        });
    }

    // Queues `fn` (`T()` or `T(const JobToken&)`). Higher `priority` runs first, equal priorities in submit
    // order; `JobCall::set_priority` moves a job that has not started. `T` is not void and is move-constructible.
    template <typename Fn>
    [[nodiscard]] JobCall<detail::JobFnResultT<std::decay_t<Fn>>> submit(Fn&& fn, int priority = 0) {
        using F = std::decay_t<Fn>;
        using T = detail::JobFnResultT<F>;
        static_assert(!std::is_void_v<T>, "a job returns a value; return a bool or a small struct");
        static_assert(std::is_move_constructible_v<T>, "a job result must be move-constructible");
        auto* const box = new detail::JobBox<T, F>(std::forward<Fn>(fn));
        detail::JobResult<T>* const slot = box;
        return JobCall<T>{submit_impl(slot, &detail::JobBox<T, F>::run, &detail::JobBox<T, F>::destroy, priority)};
    }

    // Main thread, once per frame (`GameLoop::tick`). With no workers it first runs queued jobs here, highest
    // priority first: at least one, then more until `inline_budget` has passed. Then it hands every finished
    // result to its call.
    ENGINE_API void poll(std::chrono::microseconds inline_budget = kJobInlineBudget);

    // Blocks until no job is queued or running. With no workers it runs the whole queue on the caller. Results
    // still wait for `poll`.
    ENGINE_API void wait_idle();

    // Cancels every job of the process (queued, running, finished but not polled), destroys their functions
    // and results, and returns only after no job is running. A running job that ignores its token is waited
    // for. The editor calls it on Stop, before the game module is unloaded.
    ENGINE_API void cancel_all();

    // `cancel_all`, then joins the workers. A later `parallel_for` runs on the caller and a later job in
    // `poll`. Safe to call more than once, from the owner thread, when no `parallel_for` is on the stack.
    ENGINE_API void stop();

    [[nodiscard]] ENGINE_API int worker_count() const noexcept;

private:
    friend class JobHandle;

    using RangeFn = void (*)(void* context, std::size_t begin, std::size_t end);

    ENGINE_API void parallel_for_impl(std::size_t count, std::size_t grain, void* context, RangeFn fn);
    [[nodiscard]] ENGINE_API JobHandle submit_impl(
            void* slot, detail::JobRunFn run, detail::JobDestroyFn destroy, int priority);

    // JobHandle side. `state.owner` is this system.
    void cancel_job(JobState& state);
    void reprioritize_job(JobState& state, int priority);
    void check_owner_thread() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
