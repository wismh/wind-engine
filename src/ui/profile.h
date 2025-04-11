#pragma once

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/ui/document.h>

#include <chrono>
#include <cstdint>

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
    // A tool canvas, an empty entity, or a profiler that is neither open nor CLI-capturing records
    // nothing and does not read the clock.
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

    // Points the scopes at `world` while its profiler is on. A later paint in the same frame has no
    // World of its own; the scopes write through this.
    void profiler_attach(ecs::World &world);

    // Pushes the open frame into the rings, unless Pause is on (then the open frame is dropped).
    void profiler_commit_frame(ecs::World &world);

    // Counts elements after the paint timer and records whether layout ran. Not part of the paint time.
    void profiler_finish_paint(const ecs::Entity &canvas, const Element &root, bool layout_ran);

    struct ProfileSample {
        bool stored = false;
        int frames = 0;
        bool layout_ran = false;
        bool saw_paint = false;
        bool saw_bindings = false;
        int elements = 0;
        int generated = 0;
    };

    [[nodiscard]] ProfileSample profiler_canvas_sample(ecs::World &world, ecs::Entity canvas);
    [[nodiscard]] ProfileSample profiler_shared_sample(ecs::World &world);
    [[nodiscard]] ecs::Entity profiler_selected(ecs::World &world);

    // CLI capture records the same rings as the open window, without opening it. `ready` is true
    // once a frame committed while capture was on, the window already has samples, or Pause is on.
    void profiler_cli_set_capture(ecs::World &world, bool on);
    [[nodiscard]] bool profiler_cli_ready(ecs::World &world);
    // Result object (not the ok/error envelope): paused, capturing, canvases, shared stages.
    [[nodiscard]] std::string profiler_cli_json(ecs::World &world);
#else
#define ENGINE_UI_PROFILE(canvas, stage) ((void) 0)
#define ENGINE_UI_PROFILE_SHARED(stage) ((void) 0)
#endif

} // namespace engine::ui
