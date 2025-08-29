#pragma once

// docs/tech/features/UI Profiler.md

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

    // Painter calls of a canvas's paint, one bucket per drawing or state call of the UI painter. A solid fill is
    // split by whether it has a corner radius, a gradient fill by gradient kind. Text measuring is not counted.
    enum class ProfilerPaintKind : std::uint8_t {
        Save,
        Restore,
        Scissor,
        Transform,
        View,
        Opacity,
        FillRect,
        FillRoundedRect,
        LinearGradient,
        RadialGradient,
        ConicGradient,
        StrokeRect,
        Line,
        Arc,
        Path,
        Font,
        Text,
        Image,
        ImageRepeat,
        NineSlice,
    };

    inline constexpr std::size_t kProfilerPaintKindCount = 20;

    // The kind's name in `wind-cli profile` and the editor panel.
    [[nodiscard]] constexpr std::string_view profiler_paint_kind_name(ProfilerPaintKind kind) {
        switch (kind) {
            case ProfilerPaintKind::Save:
                return "save";
            case ProfilerPaintKind::Restore:
                return "restore";
            case ProfilerPaintKind::Scissor:
                return "scissor";
            case ProfilerPaintKind::Transform:
                return "transform";
            case ProfilerPaintKind::View:
                return "view";
            case ProfilerPaintKind::Opacity:
                return "opacity";
            case ProfilerPaintKind::FillRect:
                return "fill_rect";
            case ProfilerPaintKind::FillRoundedRect:
                return "fill_rounded_rect";
            case ProfilerPaintKind::LinearGradient:
                return "linear_gradient";
            case ProfilerPaintKind::RadialGradient:
                return "radial_gradient";
            case ProfilerPaintKind::ConicGradient:
                return "conic_gradient";
            case ProfilerPaintKind::StrokeRect:
                return "stroke_rect";
            case ProfilerPaintKind::Line:
                return "line";
            case ProfilerPaintKind::Arc:
                return "arc";
            case ProfilerPaintKind::Path:
                return "path";
            case ProfilerPaintKind::Font:
                return "font";
            case ProfilerPaintKind::Text:
                return "text";
            case ProfilerPaintKind::Image:
                return "image";
            case ProfilerPaintKind::ImageRepeat:
                return "image_repeat";
            case ProfilerPaintKind::NineSlice:
                return "nine_slice";
        }
        return "";
    }

    // One committed frame of one canvas.
    struct ProfilerFrame {
        std::array<std::int64_t, kProfilerStageCount> stage_ns{};
        bool layout_ran = false;
        bool saw_paint = false;
        bool saw_bindings = false;
        int elements = 0;
        int generated = 0;
        // Painter calls by kind, base pass and popup layer together.
        std::array<int, kProfilerPaintKindCount> paint_commands{};
        // GPU draw calls the painter queued while this canvas painted. With NanoVG: the `glDrawArrays` its
        // `nvgEndFrame` flush issues for those calls. A painter that does not report them leaves 0.
        int draw_calls = 0;

        [[nodiscard]] std::int64_t ns(ProfilerStage stage) const {
            return stage_ns[static_cast<std::size_t>(stage)];
        }
        [[nodiscard]] int paint(ProfilerPaintKind kind) const {
            return paint_commands[static_cast<std::size_t>(kind)];
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

    // Empties every ring of `world`. The open frame, the selection, Pause, and recording stay, so the next commit
    // stores a whole tick. A benchmark clears after its warmup frames.
    void profiler_clear(ecs::World &world);

    // The snapshot `wind-cli profile` returns, as one JSON object: paused, capturing, per canvas the stage
    // times, draw calls, and painter calls (last, average, max over its ring), and the shared stages.
    [[nodiscard]] std::string profiler_json(ecs::World &world);
#else
    // Without ENGINE_UI_PROFILER (an exported game's Release and MinSizeRel): no scopes, no rings. Every call
    // compiles away. The editor build (ENGINE_EDITOR) has the profiler in every configuration.
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

    inline void profiler_clear(ecs::World &) {}

    [[nodiscard]] inline std::string profiler_json(ecs::World &) { return {}; }
#endif

} // namespace engine::ui
