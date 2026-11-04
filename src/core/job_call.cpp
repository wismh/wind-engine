#include <engine/core/job_call.h>

#include "core/job_state.h"

#include <engine/core/job_system.h>

#include <utility>

namespace engine {

JobHandle::JobHandle(std::shared_ptr<JobState> state) : state_(std::move(state)) {}

JobHandle::JobHandle(JobHandle&& other) noexcept = default;

JobHandle& JobHandle::operator=(JobHandle&& other) noexcept {
    if (this != &other) {
        cancel();
        state_ = std::move(other.state_);
    }
    return *this;
}

JobHandle::~JobHandle() {
    cancel();
}

JobHandle JobHandle::resolved(void* slot, detail::JobDestroyFn destroy) {
    auto state = std::make_shared<JobState>();
    state->slot = slot;
    state->destroy = destroy;
    state->status = JobStatus::Delivered;
    state->delivered = true;
    return JobHandle{std::move(state)};
}

bool JobHandle::pending() const {
    return state_ != nullptr && !state_->delivered && !state_->cancelled.load(std::memory_order_acquire);
}

void* JobHandle::delivered_slot() const {
    if (state_ == nullptr || !state_->delivered) {
        return nullptr;
    }
    return state_->slot;
}

void JobHandle::release() {
    cancel();
}

void JobHandle::cancel() {
    if (state_ == nullptr) {
        return;
    }
    if (state_->delivered) {
        // Out of the system: the slot belongs to this handle.
        if (state_->slot != nullptr) {
            void* const slot = state_->slot;
            state_->slot = nullptr;
            state_->destroy(slot);
        }
    } else if (JobSystem* const owner = state_->owner.load(std::memory_order_acquire)) {
        owner->cancel_job(*state_);
    }
    state_.reset();
}

void JobHandle::set_priority(int priority) {
    if (state_ == nullptr || state_->delivered) {
        return;
    }
    if (JobSystem* const owner = state_->owner.load(std::memory_order_acquire)) {
        owner->reprioritize_job(*state_, priority);
    }
}

}
