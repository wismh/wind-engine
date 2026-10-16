#pragma once

#include "opening_view_model.h"

#include <engine/core/window_control.h>
#include <engine/ecs/entity.h>

#include <memory>
#include <span>
#include <string>

namespace engine::ecs {
class World;
}

namespace editor {

// The startup build overlay over the editor window (order 100, above the dock space and its panels).
// While closed, its canvas is UiFit::Fixed with an empty rect so it takes no clicks. While open, it fills the
// editor window, shows the CMake configure/build progress and latest output line, and lets the user skip the
// startup build. Holds `this` in its Skip command, so it never moves.
class OpeningOverlay {
public:
    static constexpr int kCanvasOrder = 100;
    static constexpr float kTrackWidth = 480.0f;

    OpeningOverlay();

    OpeningOverlay(const OpeningOverlay&) = delete;
    OpeningOverlay& operator=(const OpeningOverlay&) = delete;

    [[nodiscard]] const std::shared_ptr<OpeningViewModel>& view_model() const;

    // Spawns the overlay canvas on `window` in `world`, initially closed (Fixed with an empty rect).
    void spawn(engine::ecs::World& world, engine::WindowId window);

    // Opens the overlay for `project_name` and `target`, setting the canvas to FillWindow and resetting progress.
    void open(std::string project_name, std::string target);
    // Closes the overlay and sets its canvas back to Fixed with an empty rect.
    void close();
    [[nodiscard]] bool is_open() const noexcept;

    // Updates the step, progress bar, and latest output line from `ProjectBuild::poll` output lines.
    void feed_lines(std::span<const std::string> lines);

    void skip();
    [[nodiscard]] bool take_skip_requested();

    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] engine::ecs::Entity canvas() const noexcept;

private:
    void set_progress(float value);
    void sync_canvas();

    std::shared_ptr<OpeningViewModel> view_model_;
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity canvas_{};
    std::string target_;
    float progress_ = 0.0f;
    bool open_ = false;
    bool skip_requested_ = false;
};

}
