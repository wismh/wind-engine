#include <engine/process/process_call.h>

#include "process/process_call_state.h"

#include <utility>

namespace engine {

ProcessCall::ProcessCall(std::shared_ptr<ProcessCallState> state)
    : state_(std::move(state)) {}

ProcessCall& ProcessCall::operator=(ProcessCall&& other) noexcept {
    if (this != &other) {
        cancel();
        state_ = std::move(other.state_);
    }
    return *this;
}

ProcessCall::~ProcessCall() {
    cancel();
}

ProcessCall ProcessCall::resolved(ProcessResult result, std::vector<std::string> output) {
    auto state = std::make_shared<ProcessCallState>();
    state->output = std::move(output);
    state->result = std::move(result);
    return ProcessCall{std::move(state)};
}

bool ProcessCall::pending() const {
    return state_ != nullptr && !state_->result.has_value() && !state_->cancelled();
}

std::vector<std::string> ProcessCall::take_output() {
    if (state_ == nullptr) {
        return {};
    }
    return std::exchange(state_->output, {});
}

std::optional<ProcessResult> ProcessCall::take() {
    if (state_ == nullptr || !state_->result.has_value() || state_->result_taken) {
        return std::nullopt;
    }
    state_->result_taken = true;
    return std::move(state_->result);
}

void ProcessCall::cancel() {
    if (state_ == nullptr || state_->result.has_value()) {
        return;
    }
    state_->cancel();
    state_.reset();
}

}
