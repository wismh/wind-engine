#pragma once

// docs/tech/modules/Core.md

#include <engine/core/application_state.h>

namespace engine {

enum class AppLifecycleEvent {
    WillEnterBackground,
    DidEnterForeground,
    Terminating,
};

void apply_app_lifecycle(ApplicationState& app, AppLifecycleEvent event);

}
