#pragma once

#include <engine/core/application_state.h>
#include <engine/resources/fatal_error.h>

#include <string_view>

namespace engine {

class SdlFatalError final : public IFatalError {
public:
    void attach(ApplicationState& app, void* native_window = nullptr);
    void report(std::string_view message) override;

private:
    ApplicationState* app_ = nullptr;
    void* native_window_ = nullptr;
};

}
