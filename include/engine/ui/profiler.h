#pragma once

// docs/tech/features/UI Profiler.md

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::ui {

    // Per-canvas stages, in the order a chart stacks them from the bottom.
    enum class ProfilerStage : std::uint8_t {
        Bindings,
        Stylesheets,
        Input,
        Layout,
        Motion,
        Paint,
    };

    inline constexpr std::size_t kProfilerStageCount = 6;

    // Frames each ring keeps.
    inline constexpr int kProfilerRingFrames = 120;

    // One committed frame of one canvas.
    struct ProfilerFrame {
        std::array<std::int64_t, kProfilerStageCount> stage_ns{};
        bool layout_ran = false;
        bool saw_paint = false;
        bool saw_bindings = false;
        int elements = 0;
        int generated = 0;

        [[nodiscard]] std::int64_t ns(ProfilerStage stage) const {
            return stage_ns[static_cast<std::size_t>(stage)];
        }
        // Painted and the dirty gate skipped layout.
        [[nodiscard]] bool layout_skipped() const { return saw_paint && !layout_ran; }
    };

    // One committed frame of the work not attributed to a canvas.
    struct ProfilerSharedFrame {
        std::int64_t begin_frame_ns = 0;
        std::int64_t commands_ns = 0;
    };

    // One canvas of the profiled world.
    struct ProfilerCanvas {
        ecs::Entity canvas{};
        WindowId window = kPrimaryWindow;
        // The root id, or `Canvas` when it is empty. Prefixed with `[window] ` when canvases of the world
        // sit on more than one window.
        std::string label;
        bool selected = false;
        // Frames in its ring.
        int frames = 0;
    };

#if defined(ENGINE_UI_PROFILER)
    inline constexpr bool kUiProfilerBuilt = true;

    // Starts or stops recording `world` for a panel. One world records at a time: the last one attached
    // or captured by wind-cli. Detaching drops the rings unless CLI capture is on, and clears the
    // selection and Pause. Does not bind a key, open a window, or spawn a canvas.
    void set_ui_profiler_attached(ecs::World &world, bool attached);

    [[nodiscard]] bool ui_profiler_attached(ecs::World &world);

    // Every canvas with a live tree, by window, then `order`, then entity index. When the selected canvas
    // is gone, the first one becomes selected.
    [[nodiscard]] std::vector<ProfilerCanvas> profiler_canvases(ecs::World &world);

    void profiler_select(ecs::World &world, ecs::Entity canvas);

    [[nodiscard]] ecs::Entity profiler_selected(ecs::World &world);

    // While paused, each commit drops the open frame and the rings stay as they are.
    void set_profiler_paused(ecs::World &world, bool paused);

    [[nodiscard]] bool profiler_paused(ecs::World &world);

    // The ring of `canvas`, oldest first. Empty when it has no frames.
    [[nodiscard]] std::vector<ProfilerFrame> profiler_frames(ecs::World &world, ecs::Entity canvas);

    // The shared ring, oldest first.
    [[nodiscard]] std::vector<ProfilerSharedFrame> profiler_shared_frames(ecs::World &world);
#else
    // Release and MinSizeRel: no scopes, no rings. Every call compiles away.
    inline constexpr bool kUiProfilerBuilt = false;

    inline void set_ui_profiler_attached(ecs::World &, bool) {}

    [[nodiscard]] inline bool ui_profiler_attached(ecs::World &) { return false; }

    [[nodiscard]] inline std::vector<ProfilerCanvas> profiler_canvases(ecs::World &) { return {}; }

    inline void profiler_select(ecs::World &, ecs::Entity) {}

    [[nodiscard]] inline ecs::Entity profiler_selected(ecs::World &) { return {}; }

    inline void set_profiler_paused(ecs::World &, bool) {}

    [[nodiscard]] inline bool profiler_paused(ecs::World &) { return false; }

    [[nodiscard]] inline std::vector<ProfilerFrame> profiler_frames(ecs::World &, ecs::Entity) { return {}; }

    [[nodiscard]] inline std::vector<ProfilerSharedFrame> profiler_shared_frames(ecs::World &) { return {}; }
#endif

} // namespace engine::ui
