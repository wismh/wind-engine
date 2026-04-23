#pragma once

#include "editor_options.h"
#include "editor_panels.h"
#include "engine_host_play.h"
#include "play_session.h"
#include "toolbar.h"

#include <engine/core/engine_host.h>
#include <engine/core/file_dialog.h>
#include <engine/core/window_desc.h>

#include <filesystem>
#include <optional>
#include <string>

namespace engine::ecs {
class World;
}

namespace editor {

// The editor process: one EngineHost, the editor's world and window, the toolbar, the Inspector and
// Profiler panels, and the play session.
// kPrimaryWindow belongs to the game being played and is empty between plays. Holds `this` in its run
// hooks and systems, so it never moves.
class EditorApp {
public:
    EditorApp() = default;
    ~EditorApp();

    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    [[nodiscard]] int run(const EditorOptions& options);

private:
    [[nodiscard]] bool start(const EditorOptions& options);
    void read_events(engine::ecs::World& world);
    void on_frame_end();
    void on_quit();

    void choose_game();
    void take_dialog_answer(const engine::FileDialogResult& answer);
    void play();
    void stop(std::string status);

    // Declared first, destroyed last: everything below holds references into its services. The panels
    // outlive the session, whose Stop detaches them.
    engine::EngineHost host_;
    std::optional<Toolbar> toolbar_;
    std::optional<EditorPanels> panels_;
    std::optional<EngineHostPlay> play_host_;
    std::optional<PlaySession> session_;
    engine::ecs::World* world_ = nullptr;
    engine::WindowId window_{};

    std::filesystem::path game_path_;
    engine::FileDialogCall dialog_;
    bool play_at_start_ = false;
    bool close_requested_ = false;
    bool quitting_ = false;
};

}
