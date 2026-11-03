#pragma once

// docs/tech/modules/Core.md

#include <engine/core/export.h>

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace engine {

struct JobState;

// Handed to a job function that takes one. `cancelled` turns true once the job's call is cancelled or dropped,
// so a long job can return early. Readable from the worker. The engine builds it.
class JobToken {
public:
    explicit JobToken(const std::atomic<bool>& cancelled) noexcept : cancelled_(&cancelled) {}

    [[nodiscard]] bool cancelled() const noexcept {
        return cancelled_->load(std::memory_order_acquire);
    }

private:
    const std::atomic<bool>* cancelled_;
};

namespace detail {

// What a `JobCall<T>` reads. The engine sees it only as `void*`.
template <typename T>
struct JobResult {
    std::optional<T> value;
};

// Runs the job and stores its result into the slot. Destroys the slot (function and result).
using JobRunFn = void (*)(void* slot, const JobToken& token) noexcept;
using JobDestroyFn = void (*)(void* slot) noexcept;

}

// The non-template part of every `JobCall`. Move-only. Main thread only. Destroying it cancels the job.
class JobHandle {
public:
    JobHandle() = default;
    // Engine side: `JobSystem::submit` builds handles from its own state.
    ENGINE_API explicit JobHandle(std::shared_ptr<JobState> state);

    JobHandle(const JobHandle&) = delete;
    JobHandle& operator=(const JobHandle&) = delete;
    ENGINE_API JobHandle(JobHandle&& other) noexcept;
    ENGINE_API JobHandle& operator=(JobHandle&& other) noexcept;
    ENGINE_API ~JobHandle();

    // A handle that already holds a delivered slot. For `JobCall::resolved`.
    [[nodiscard]] ENGINE_API static JobHandle resolved(void* slot, detail::JobDestroyFn destroy);

    // Submitted, not cancelled, and not delivered yet.
    [[nodiscard]] ENGINE_API bool pending() const;

    // The slot once `JobSystem::poll` has delivered it, otherwise null.
    [[nodiscard]] ENGINE_API void* delivered_slot() const;

    // Destroys the slot and empties the handle.
    ENGINE_API void release();

    // Queued: never runs. Running: the token turns true and the result is discarded. Finished: the result is
    // discarded. Never waits. An empty handle ignores it.
    ENGINE_API void cancel();

    // Higher runs first. Moves a queued job; a running or finished one ignores it.
    ENGINE_API void set_priority(int priority);

private:
    std::shared_ptr<JobState> state_;
};

// One background job, owned by whoever submitted it (`JobSystem::submit`). Move-only. Destroying a call that is
// still pending cancels it, so a call kept in a component, a view-model, or a game object dies with its owner.
//
// The result becomes visible at the start of a frame (`JobSystem::poll`), so every system of that frame sees the
// same state. Main thread only.
template <typename T>
class JobCall {
public:
    JobCall() = default;
    explicit JobCall(JobHandle handle) : handle_(std::move(handle)) {}

    // A call that already holds `value`. For fakes and tests of game code.
    [[nodiscard]] static JobCall resolved(T value) {
        auto* const slot = new detail::JobResult<T>{std::optional<T>{std::move(value)}};
        return JobCall{JobHandle::resolved(slot, &destroy_resolved)};
    }

    // Submitted, not cancelled, and no result yet.
    [[nodiscard]] bool pending() const {
        return handle_.pending();
    }

    // The result once, then the call is empty. nullopt while pending, after cancel, and on an empty call.
    [[nodiscard]] std::optional<T> take() {
        void* const slot = handle_.delivered_slot();
        if (slot == nullptr) {
            return std::nullopt;
        }
        std::optional<T> result = std::move(static_cast<detail::JobResult<T>*>(slot)->value);
        handle_.release();
        return result;
    }

    // See `JobHandle::cancel`.
    void cancel() {
        handle_.cancel();
    }

    // See `JobHandle::set_priority`.
    void set_priority(int priority) {
        handle_.set_priority(priority);
    }

private:
    static void destroy_resolved(void* slot) noexcept {
        delete static_cast<detail::JobResult<T>*>(slot);
    }

    JobHandle handle_;
};

}
