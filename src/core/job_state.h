#pragma once

#include <engine/core/job_call.h>

#include <atomic>
#include <cstdint>

namespace engine {

class JobSystem;

enum class JobStatus {
    Queued,
    Running,
    // Returned on a worker, waiting for `JobSystem::poll`.
    Finished,
    // Handed to the call by `poll`, or built resolved.
    Delivered,
    // Cancelled and out of the system. The slot is gone or about to go.
    Dropped,
};

// One submitted job, shared by its `JobHandle` and the `JobSystem` queue. Created by `make_shared` inside the
// engine, so the last reference may go after a game module is unloaded.
//
// While `owner` is set, `status`, `slot`, and `priority` are guarded by the system's mutex. Once the
// system lets go (`owner` null), they belong to the main thread. `delivered` is main-thread only.
//
// `slot` holds the function and the result, built by template code in whatever module submitted it. Only
// `run` and `destroy` touch it, and `destroy` runs exactly once: on the main thread when a queued or finished
// job is cancelled or its result is taken, on the worker when a running job was cancelled.
struct JobState {
    std::atomic<bool> cancelled{false};
    JobStatus status = JobStatus::Queued;
    bool delivered = false;
    void* slot = nullptr;
    detail::JobRunFn run = nullptr;
    detail::JobDestroyFn destroy = nullptr;
    int priority = 0;
    std::uint64_t seq = 0;
    // Atomic because a worker clears it when a job cancelled by `cancel_all` returns, while the call may still
    // look at it on the main thread. The system itself only goes away on the main thread.
    std::atomic<JobSystem*> owner{nullptr};
};

}
