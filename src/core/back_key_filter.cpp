#include "core/back_key_filter.h"

namespace engine {

BackKeyFilter::Route BackKeyFilter::route(KeyCode key, bool down, bool repeat, bool text_input_active) {
    if (key != KeyCode::AcBack) {
        return Route::Deliver;
    }
    if (down && !repeat) {
        dismissing_ = text_input_active;
        return dismissing_ ? Route::DismissTextInput : Route::Deliver;
    }
    if (!dismissing_) {
        return Route::Deliver;
    }
    if (!down) {
        dismissing_ = false;
    }
    return Route::Swallow;
}

}
