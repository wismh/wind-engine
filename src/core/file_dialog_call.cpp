#include <engine/core/file_dialog.h>

#include "core/file_dialog_state.h"

#include <utility>

namespace engine {

FileDialogCall::FileDialogCall(std::shared_ptr<FileDialogState> state)
    : state_(std::move(state)) {}

FileDialogCall& FileDialogCall::operator=(FileDialogCall&& other) noexcept {
    if (this != &other) {
        cancel();
        state_ = std::move(other.state_);
    }
    return *this;
}

FileDialogCall::~FileDialogCall() {
    cancel();
}

FileDialogCall FileDialogCall::resolved(FileDialogResult result) {
    auto state = std::make_shared<FileDialogState>();
    state->result = std::move(result);
    return FileDialogCall{std::move(state)};
}

bool FileDialogCall::pending() const {
    return state_ != nullptr && !state_->result.has_value() && !state_->cancelled();
}

std::optional<FileDialogResult> FileDialogCall::take() {
    if (state_ == nullptr || !state_->result.has_value()) {
        return std::nullopt;
    }
    std::optional<FileDialogResult> result = std::move(state_->result);
    state_.reset();
    return result;
}

void FileDialogCall::cancel() {
    if (state_ != nullptr && !state_->result.has_value()) {
        state_->cancel();
    }
    state_.reset();
}

}
