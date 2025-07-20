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
        ShowSdkFolder,
        BrowseLocation,
        CreateProject,
        Open,
        Remove,
        ShowSdk,
        ForgetSdk,
        DeleteSdk,
    };

    Kind kind = Kind::None;
    // The project row (Open, Remove) or SDK row (ShowSdk, ForgetSdk, DeleteSdk).
    std::size_t index = 0;
};

}
