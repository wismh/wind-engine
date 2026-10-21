#pragma once

// docs/tech/modules/Core.md

#include <engine/core/export.h>

#include <cstddef>
#include <memory>
#include <type_traits>

namespace engine {

// Fork-join pool. `parallel_for` is called from the thread that owns the system and returns only after every
// index has been visited once. With no workers, and on Web without pthreads, the ranges run on the caller.
// A `parallel_for` made from inside a running range also runs on that thread.
//
// The function must not throw and must not call engine APIs. It may read what the caller shares and write
// disjoint indices of a buffer the caller owns.
class JobSystem {
public:
    // `workers` < 0 is `hardware_concurrency - 1`, or 0 when the machine has one core (and always 0 on Web
    // without pthreads). `workers` == 0 runs every range on the caller.
    ENGINE_API explicit JobSystem(int workers = -1);
    ENGINE_API ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Half-open ranges of at most `grain` indices. `grain` == 0 is one range covering `count`.
    template<typename Fn>
    void parallel_for(std::size_t count, std::size_t grain, Fn&& fn) {
        auto* context = &fn;
        parallel_for_impl(count, grain, context, [](void* ctx, std::size_t begin, std::size_t end) {
            (*static_cast<std::remove_reference_t<Fn>*>(ctx))(begin, end);
        });
    }

    // Joins the workers. A later `parallel_for` runs on the caller. Safe to call more than once, from the
    // owner thread, when no `parallel_for` is on the stack.
    ENGINE_API void stop();

    [[nodiscard]] ENGINE_API int worker_count() const noexcept;

private:
    using RangeFn = void (*)(void* context, std::size_t begin, std::size_t end);

    ENGINE_API void parallel_for_impl(std::size_t count, std::size_t grain, void* context, RangeFn fn);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
