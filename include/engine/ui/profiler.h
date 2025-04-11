#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <functional>
#include <optional>

namespace engine::ui {

    // Marks the profiler's own canvas so timing, and the profiler's canvas list, skip it.
    struct ProfilerPanel {
        WindowId window = kPrimaryWindow;
    };

    // Installed by the windowed presentation. Empty in headless tests: the panel still gets a
    // canvas and a WindowSizes entry, and no OS window is opened.
    struct ProfilerWindowHost {
        std::function<std::optional<WindowId>(const WindowDesc &)> open;
        std::function<void(WindowId)> close;
    };

    // Opens or closes the profiler window. Does not bind a key; the game calls this.
    // Without ENGINE_UI_PROFILER (Release and MinSizeRel) the call compiles away.
#if defined(ENGINE_UI_PROFILER)
    void set_ui_profiler_enabled(ecs::World &world, bool enabled);

    [[nodiscard]] bool ui_profiler_enabled(ecs::World &world);

    // Opens the profiler window if it is not up yet. Called from begin_frame, after the timed
    // section, so the tool is not inside the shared begin_frame sample.
    void sync_profiler_frames(ecs::World &world);

    // Rebuilds the canvas list and the selected canvas's numbers. Called at the start of Bind.
    void sync_profiler_content(ecs::World &world);
#else
    inline void set_ui_profiler_enabled(ecs::World &, bool) {}

    [[nodiscard]] inline bool ui_profiler_enabled(ecs::World &) { return false; }

    inline void sync_profiler_frames(ecs::World &) {}

    inline void sync_profiler_content(ecs::World &) {}
#endif

} // namespace engine::ui
