#pragma once

#include <engine/core/application_state.h>

namespace engine {

enum class AppLifecycleEvent {
    WillEnterBackground,
    DidEnterForeground,
    Terminating,
};

void apply_app_lifecycle(ApplicationState& app, AppLifecycleEvent event);

void apply_android_back(ApplicationState& app, bool text_input_active);

[[nodiscard]] constexpr bool android_back_quits() noexcept {
    return true;
}

}
