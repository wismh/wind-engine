#include <engine/core/app_lifecycle.h>

namespace engine {

void apply_app_lifecycle(ApplicationState& app, AppLifecycleEvent event) {
    switch (event) {
        case AppLifecycleEvent::WillEnterBackground:
            app.paused = true;
            break;
        case AppLifecycleEvent::DidEnterForeground:
            app.paused = false;
            break;
        case AppLifecycleEvent::Terminating:
            app.quit();
            break;
    }
}

void apply_android_back(ApplicationState& app, bool text_input_active) {
    if (text_input_active) {
        return;
    }
    if (android_back_quits()) {
        app.quit();
    }
}

}
