#pragma once

#include "editor_view_model.h"

#include <memory>
#include <string>

namespace editor {

// What a toolbar button asked for. The editor acts on it at the end of the frame, never inside the
// UI pass that ran the command.
enum class EditorRequest {
    None,
    Play,
    Stop,
    // Build the standalone game and copy it out.
    Export,
};

// What the Play button does now.
enum class RunState {
    // Play builds and starts the project.
    Idle,
    // A build is running; the button cancels it.
    Building,
    // The game runs; the button stops it.
    Playing,
};

// The editor window's top bar: Play/Stop, status line, and project line. Owns the view-model and the button
// methods. Holds `this` in its commands, so it never moves.
class Toolbar {
public:
    Toolbar();

    Toolbar(const Toolbar&) = delete;
    Toolbar& operator=(const Toolbar&) = delete;

    [[nodiscard]] const std::shared_ptr<EditorViewModel>& view_model() const;

    void toggle_play();
    [[nodiscard]] bool can_toggle_play() const;

    // The Export button: only when nothing runs (a build shows Cancel on the Play button).
    void export_game();
    [[nodiscard]] bool can_export_game() const;

    // The project line. `playable` enables Play and Export: the project was read and fits this editor's SDK.
    void show_project(std::string text, bool playable);
    void show_status(std::string text);
    void show_state(RunState state);

    // The last request since the previous call, then None.
    [[nodiscard]] EditorRequest take_request();

    [[nodiscard]] RunState state() const;
    // The open project fits this editor's SDK, so Play can build it.
    [[nodiscard]] bool playable() const;
    [[nodiscard]] const std::string& status() const;

private:
    std::shared_ptr<EditorViewModel> view_model_;
    EditorRequest request_ = EditorRequest::None;
    RunState state_ = RunState::Idle;
    bool playable_ = false;
};

}
