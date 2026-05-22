#pragma once

#include <cstddef>

namespace launcher {

// What a button asked for. The launcher acts on it in its frame system, never inside the UI pass that ran the
// command: acting rebuilds the rows, and the row whose button ran would go away under it.
struct LauncherRequest {
    enum class Kind {
        None,
        AddProject,
        LocateSdk,
        Open,
        Remove,
        Forget,
    };

    Kind kind = Kind::None;
    // The project row (Open, Remove) or editor row (Forget).
    std::size_t index = 0;
};

}
