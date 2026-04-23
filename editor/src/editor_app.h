#pragma once

#include "edit_module.h"
#include "editor_cli.h"
#include "editor_options.h"
#include "editor_panels.h"
#include "engine_host_play.h"
#include "opening_overlay.h"
#include "play_session.h"
#include "project_build.h"
#include "toolbar.h"

#include <engine/core/engine_host.h>
#include <engine/core/window_desc.h>
#include <engine/project/sdk_manifest.h>
#include <engine/project/wind_project.h>
#include <engine/ui/ui_window.h>

#include <filesystem>
#include <optional>
#include <string>

namespace engine::ecs {
class World;
}

namespace editor {

// The editor process: one EngineHost, the editor's world and window, the toolbar, the Project, UI Tree,
// Inspector, Profiler, and Build panels, the opening overlay, the open project, its build, the Edit Mode
// module and catalog, and the play session. Opening a project builds its game module and loads it into Edit
// Mode; Play builds the module against this editor's SDK and runs it.
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
    void on_frame_end();
    void on_quit();
    [[nodiscard]] EditorFacts facts() const;

    void open_project(const std::filesystem::path& directory);
    void start_opening_build();
    void load_edit_module(const std::filesystem::path& module);
    void play();
    void export_game();
    void start_export(const ExportChoice& choice);
    void start_build(BuildKind kind, bool development = false);
    void poll_build();
    void start_game(const std::filesystem::path& module);
    void finish_export(const std::filesystem::path& built);
    void stop(std::string status);

    // Declared first, destroyed last: everything below holds references into its services. The panels
    // outlive the session, whose Stop detaches them.
    engine::EngineHost host_;
    std::optional<Toolbar> toolbar_;
    std::optional<EditorCli> cli_;
    std::optional<EditorPanels> panels_;
    std::optional<OpeningOverlay> opening_overlay_;
    std::optional<EngineHostPlay> play_host_;
    std::optional<EditModule> edit_module_;
    std::optional<PlaySession> session_;
    std::optional<ProjectBuild> build_;
    engine::ecs::World* world_ = nullptr;
    // The editor window: open, bound to `world_`, with the editor canvas. Closed with the app, before `host_` goes.
    std::optional<engine::UiWindow> window_ui_;
    engine::WindowId window_{};

    // The SDK this editor runs from: `<sdk>/bin/wind_editor.exe`. No manifest: not an installed SDK, so it cannot
    // build projects.
    std::filesystem::path sdk_root_;
    std::optional<engine::SdkManifest> sdk_;
    // The open project, once read and checked against the SDK.
    std::filesystem::path project_dir_;
    std::optional<engine::WindProject> project_;
    // What the build that runs, or ran last, makes, and for an Export where its output goes.
    BuildKind building_ = BuildKind::Module;
    bool opening_build_ = false;
    std::filesystem::path export_directory_;
    bool play_at_start_ = false;
    bool quitting_ = false;
};

}
