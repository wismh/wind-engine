#pragma once

#include "profiler_row_view_model.h"
#include "profiler_view_model.h"

#include <engine/ecs/entity.h>

#include <map>
#include <memory>

namespace engine::ecs {
class World;
}

namespace editor {

// The Profiler tab. Reads the engine's UI profiler rings of the game world (`engine/ui/profiler.h`) and
// shows the canvas list, the selected canvas's stage chart, the shared chart, and the numbers. The editor build
// (ENGINE_EDITOR) has the profiler in every configuration, Release included. Holds `this` in its rows, so it
// never moves.
class ProfilerPanel {
public:
    ProfilerPanel();

    ProfilerPanel(const ProfilerPanel&) = delete;
    ProfilerPanel& operator=(const ProfilerPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<ProfilerViewModel>& view_model() const;

    // Starts recording `game` (the world of kPrimaryWindow).
    void attach(engine::ecs::World& game);
    // Stops recording and clears every row and chart. Must run before the game world is destroyed.
    void detach();
    [[nodiscard]] bool attached() const;

    // Copies the rings into the view-model. Pause is two-way like the inspector's Pick.
    void refresh();

    void select(engine::ecs::Entity canvas);

private:
    void show_rings();
    void show_idle();

    std::shared_ptr<ProfilerViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    std::map<engine::ecs::Entity, std::shared_ptr<ProfilerRowViewModel>> rows_;
    bool pause_shown_ = false;
};

}
