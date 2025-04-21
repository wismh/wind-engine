#pragma once

#include "editor_tab.h"
#include "editor_view_model.h"

#include <filesystem>
#include <memory>
#include <string>

namespace editor {

// What a toolbar button asked for. The editor acts on it at the end of the frame, never inside the
// UI pass that ran the command.
enum class EditorRequest {
    None,
    ChooseGame,
    Play,
    Stop,
};

// The editor window's top bar: Choose game, Play/Stop, status line, game path, and the Inspector/Profiler
// tab strip. Owns the view-model and the button methods. Holds `this` in its commands, so it never moves.
class Toolbar {
public:
    Toolbar();

    Toolbar(const Toolbar&) = delete;
    Toolbar& operator=(const Toolbar&) = delete;

    [[nodiscard]] const std::shared_ptr<EditorViewModel>& view_model() const;

    void choose_game();
    void toggle_play();
    void show_inspector();
    void show_profiler();
    [[nodiscard]] bool can_choose_game() const;
    [[nodiscard]] bool can_toggle_play() const;

    void show_game(const std::filesystem::path& module);
    void show_status(std::string text);
    void show_playing(bool playing);

    // The last request since the previous call, then None.
    [[nodiscard]] EditorRequest take_request();

    [[nodiscard]] EditorTab active_tab() const;

private:
    void show_tab(EditorTab tab);

    std::shared_ptr<EditorViewModel> view_model_;
    EditorRequest request_ = EditorRequest::None;
    EditorTab tab_ = EditorTab::Inspector;
    bool has_game_ = false;
    bool playing_ = false;
};

}
