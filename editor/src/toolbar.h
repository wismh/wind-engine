#pragma once

#include "editor_tab.h"
#include "editor_view_model.h"

#include <memory>
#include <string>

namespace editor {

// What a toolbar button asked for. The editor acts on it at the end of the frame, never inside the
// UI pass that ran the command.
enum class EditorRequest {
    None,
    OpenProject,
    Play,
    Stop,
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

// The editor window's top bar: Open project, Play/Stop, status line, project line, and the Inspector/Profiler/Build
// tab strip. Owns the view-model and the button methods. Holds `this` in its commands, so it never moves.
class Toolbar {
public:
    Toolbar();

    Toolbar(const Toolbar&) = delete;
    Toolbar& operator=(const Toolbar&) = delete;

    [[nodiscard]] const std::shared_ptr<EditorViewModel>& view_model() const;

    void open_project();
    void toggle_play();
    void show_inspector();
    void show_profiler();
    void show_build();
    [[nodiscard]] bool can_open_project() const;
    [[nodiscard]] bool can_toggle_play() const;

    // The project line. `playable` enables Play: the project was read and fits this editor's SDK.
    void show_project(std::string text, bool playable);
    void show_status(std::string text);
    void show_state(RunState state);

    // The last request since the previous call, then None.
    [[nodiscard]] EditorRequest take_request();

    [[nodiscard]] RunState state() const;
    // The open project fits this editor's SDK, so Play can build it.
    [[nodiscard]] bool playable() const;
    [[nodiscard]] const std::string& status() const;

    [[nodiscard]] EditorTab active_tab() const;

private:
    void show_tab(EditorTab tab);

    std::shared_ptr<EditorViewModel> view_model_;
    EditorRequest request_ = EditorRequest::None;
    EditorTab tab_ = EditorTab::Inspector;
    RunState state_ = RunState::Idle;
    bool playable_ = false;
};

}
