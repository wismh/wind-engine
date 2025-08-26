#pragma once

// docs/tech/features/UI Input.md#cursor

#include <cstdint>

namespace engine::ui {

    // CSS `cursor`. Auto takes the parent's cursor; with none set up the tree, a TextInput or a selectable Label
    // shows Text and anything else Default. The rest map to the platform's system cursors.
    enum class Cursor : std::uint8_t {
        Auto,
        Default,
        Pointer,
        Text,
        Crosshair,
        Wait,
        Progress,
        Move,
        NotAllowed,
        EwResize,
        NsResize,
        NwseResize,
        NeswResize,
    };

} // namespace engine::ui
