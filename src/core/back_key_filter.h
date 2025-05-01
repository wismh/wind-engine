#pragma once

// docs/tech/features/Input Mapper.md

#include <engine/core/key_code.h>

namespace engine {

// Engine side of the Android back key (KeyCode::AcBack). The engine never quits on Back: a game binds
// AcBack to its own ActionId. While a TextInput has the keyboard, the press only dismisses it. That press,
// its repeats, and its release never reach InputSystem, so one press does not also navigate back.
class BackKeyFilter {
public:
    enum class Route {
        Deliver,          // hand the key to InputSystem
        DismissTextInput, // clear UI focus, do not deliver
        Swallow,          // repeat or release of a dismissing press, do not deliver
    };

    [[nodiscard]] Route route(KeyCode key, bool down, bool repeat, bool text_input_active);

private:
    bool dismissing_ = false;
};

}
