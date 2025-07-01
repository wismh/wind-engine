#pragma once

#include "editor_options.h"
#include "editor_panels.h"
#include "engine_host_play.h"
#include "play_session.h"
#include "project_build.h"
#include "toolbar.h"

#include <engine/core/engine_host.h>
#include <engine/core/file_dialog.h>
#include <engine/core/window_desc.h>
#include <engine/project/sdk_manifest.h>
#include <engine/project/wind_project.h>

#include <filesystem>
#include <optional>
#include <string>

namespace engine::ecs {
class World;
}

namespace editor {

// The editor process: one EngineHost, the editor's world and window, the toolbar, the Inspector, Profiler, and
// Build panels, the open project, its build, and the play session. Play builds the project's game module with
// cmake against this editor's SDK, then loads it.
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

    void choose_project();
    void take_dialog_answer(const engine::FileDialogResult& answer);
    void open_project(const std::filesystem::path& directory);
    void play();
    void poll_build();
    void start_game(const std::filesystem::path& module);
    void stop(std::string status);

    // Declared first, destroyed last: everything below holds references into its services. The panels
    // outlive the session, whose Stop detaches them.
    engine::EngineHost host_;
    std::optional<Toolbar> toolbar_;
    std::optional<EditorPanels> panels_;
    std::optional<EngineHostPlay> play_host_;
    std::optional<PlaySession> session_;
    std::optional<ProjectBuild> build_;
    engine::ecs::World* world_ = nullptr;
    engine::WindowId window_{};

    // The SDK this editor runs from: `<sdk>/bin/wind_editor.exe`. No manifest: not an installed SDK, so it cannot
    // build projects.
    std::filesystem::path sdk_root_;
    std::optional<engine::SdkManifest> sdk_;
    // The open project, once read and checked against the SDK.
    std::filesystem::path project_dir_;
    std::optional<engine::WindProject> project_;
    engine::FileDialogCall dialog_;
    bool play_at_start_ = false;
    bool close_requested_ = false;
    bool quitting_ = false;
};

}
