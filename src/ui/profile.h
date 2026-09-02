#pragma once

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/ui/document.h>
#include <engine/ui/profiler.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace engine::ui {

    enum class ProfileStage : std::uint8_t {
        Bindings,
        Stylesheets,
        Input,
        Layout,
        Motion,
        Paint,
        BeginFrame,
        CommandBuild,
    };

#if defined(ENGINE_UI_PROFILER)
    // Times from construction to destruction. `canvas` is read in the destructor, so a caller can
    // fill it in during the scope (prepare_top_canvas does, once it knows which canvas it hit).
    // An empty entity, or a pass over a world that is not profiled, records nothing and does not read
    // the clock.
    class UiProfileScope {
    public:
        UiProfileScope(const ecs::Entity &canvas, ProfileStage stage);
        ~UiProfileScope();

        UiProfileScope(const UiProfileScope &) = delete;
        UiProfileScope &operator=(const UiProfileScope &) = delete;

    private:
        const ecs::Entity *canvas_ = nullptr;
        ProfileStage stage_ = ProfileStage::Paint;
        std::chrono::steady_clock::time_point start_{};
        bool active_ = false;
    };

    class UiProfileSharedScope {
    public:
        explicit UiProfileSharedScope(ProfileStage stage);
        ~UiProfileSharedScope();

        UiProfileSharedScope(const UiProfileSharedScope &) = delete;
        UiProfileSharedScope &operator=(const UiProfileSharedScope &) = delete;

    private:
        ProfileStage stage_ = ProfileStage::BeginFrame;
        std::chrono::steady_clock::time_point start_{};
        bool active_ = false;
    };

#define ENGINE_UI_PROFILE(canvas, stage)                                                                               \
    ::engine::ui::UiProfileScope _profile_##stage { canvas, ::engine::ui::ProfileStage::stage }
#define ENGINE_UI_PROFILE_SHARED(stage)                                                                                \
    ::engine::ui::UiProfileSharedScope _profile_shared_##stage { ::engine::ui::ProfileStage::stage }

    // Called at the start of each engine pass over `world` (begin_frame, input, bind, command build).
    // Scopes until the next call record only when `world` is the profiled one: attached or captured by
    // wind-cli. That keeps the editor's own canvases out of the game's rings.
    void profiler_attach(ecs::World &world);

    // True while scopes record: the world of the current pass is the profiled one.
    [[nodiscard]] bool profiler_recording();

    // Paint has no World. run_ui_render leaves CmdDrawUI::canvas empty unless its world is profiled, so
    // paint_document records exactly when `canvas` is set.
    void profiler_begin_paint(const ecs::Entity &canvas);

    // True while paint of `canvas` records: the canvas is set and its world is the profiled one.
    [[nodiscard]] bool profiler_records_canvas(const ecs::Entity &canvas);

    // Adds painter calls by kind and the GPU draw calls they queued to the open frame of `canvas`. Called once per
    // paint pass (base and popup layer) by ProfilerPaintCounter; records nothing unless profiler_records_canvas.
    void profiler_add_paint(const ecs::Entity &canvas, const std::array<int, kProfilerPaintKindCount> &commands,
                            int draw_calls);

    // Pushes the open frame into the rings, unless Pause is on (then the open frame is dropped).
    void profiler_commit_frame(ecs::World &world);

    // Counts elements after the paint timer and records whether layout ran. Not part of the paint time.
    void profiler_finish_paint(const ecs::Entity &canvas, const Element &root, bool layout_ran);

    // CLI capture records the same rings as an attached panel, without one. `ready` is true once a
    // frame committed while capture was on, an attached panel already has samples, or Pause is on.
    void profiler_cli_set_capture(ecs::World &world, bool on);
    [[nodiscard]] bool profiler_cli_ready(ecs::World &world);
#else
#define ENGINE_UI_PROFILE(canvas, stage) ((void) 0)
#define ENGINE_UI_PROFILE_SHARED(stage) ((void) 0)
#endif

} // namespace engine::ui
