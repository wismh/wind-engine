#pragma once

#include <engine/core/application_state.h>
#include <engine/log.h>
#include <engine/resources/fatal_error.h>

#include <cstdlib>
#include <string_view>

namespace engine {

// The fatal exit of a dedicated server: one line in the log and exit code 1. Not `abort`, whose dialog would wait
// forever on a machine nobody looks at, and not a message box. The loop does not unwind, so `on_quit` does not run.
class ServerFatalError final : public IFatalError {
public:
    void attach(ApplicationState& state) noexcept {
        state_ = &state;
    }

    [[noreturn]] void report(std::string_view message) override {
        log::error(message);
        if (state_ != nullptr) {
            state_->quit();
        }
        std::exit(1);
    }

private:
    ApplicationState* state_ = nullptr;
};

}
