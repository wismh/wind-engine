#include <engine/net/http_call.h>

#include "net/http_call_state.h"

#include <utility>

namespace engine {

HttpCall::HttpCall(std::shared_ptr<HttpCallState> state)
    : state_(std::move(state)) {}

HttpCall& HttpCall::operator=(HttpCall&& other) noexcept {
    if (this != &other) {
        cancel();
        state_ = std::move(other.state_);
    }
    return *this;
}

HttpCall::~HttpCall() {
    cancel();
}

HttpCall HttpCall::resolved(HttpResult result) {
    auto state = std::make_shared<HttpCallState>();
    state->result = std::move(result);
    return HttpCall{std::move(state)};
}

bool HttpCall::pending() const {
    return state_ != nullptr && !state_->result.has_value() && !state_->cancelled();
}

std::optional<HttpResult> HttpCall::take() {
    if (state_ == nullptr || !state_->result.has_value()) {
        return std::nullopt;
    }
    std::optional<HttpResult> result = std::move(state_->result);
    state_.reset();
    return result;
}

void HttpCall::cancel() {
    if (state_ == nullptr || state_->result.has_value()) {
        state_.reset();
        return;
    }
    state_->cancel();
    state_.reset();
}

}
