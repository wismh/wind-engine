#include "editor_app.h"

#include <asset_ids.h>

#include <engine/core/application_state.h>
#include <engine/core/platform.h>
#include <engine/core/window_control.h>
#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/log.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>

#include <system_error>
#include <utility>
#include <vector>

namespace editor {
namespace {

// kPrimaryWindow between plays. Play applies the game's own description; Stop puts title and style back.
const engine::WindowDesc kIdleGameWindow{.title = "Game", .size = {800, 600}};
const engine::WindowDesc kEditorWindow{.title = "Wind Editor", .size = {1280, 800}};
constexpr char kNoProjectMessage[] =
        "No project to open. Start the editor from the Wind launcher, or run wind_editor --project <dir>.";

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// `<sdk>/bin/assets` -> `<sdk>`.
std::filesystem::path sdk_root_of(const std::filesystem::path& assets_root) {
    std::filesystem::path root = std::filesystem::absolute(assets_root).lexically_normal();
    if (!root.has_filename()) {
        root = root.parent_path();
    }
    return root.parent_path().parent_path();
}

// `<user data>/live`, where Play puts module copies. The temp directory when there is no user data
// directory, because Play cannot load the original in place.
std::filesystem::path live_root() {
    const auto user_data = engine::user_data_directory("Wind", "Editor");
    if (user_data) {
        return *user_data / "live";
    }
    engine::log::warn("Editor: no user data directory (" + user_data.error().message() +
            "). Game module copies go to the temp directory");
    return std::filesystem::temp_directory_path() / "wind_editor" / "live";
}

}

EditorApp::~EditorApp() {
    session_.reset();
}

int EditorApp::run(const EditorOptions& options) {
    if (!start(options)) {
        return 1;
    }
    return host_.run(engine::RunHooks{
            .on_start = [this] {
                if (play_at_start_) {
                    play();
                }
            },
            .on_frame_end = [this] { on_frame_end(); },
            .on_quit = [this] { on_quit(); },
            .cli = engine::CliCommands{
                    .kind = "editor",
                    .handle = [this](const engine::CliCommand& command) { return cli_->handle(command, facts()); },
            },
    });
}

bool EditorApp::start(const EditorOptions& options) {
    if (!host_.init()) {
        return false;
    }
    if (!options.project) {
        // The launcher passes --project; there is no Open dialog to fall back on.
        host_.fatal().report(kNoProjectMessage);
        return false;
    }
    if (!host_.open_primary(kIdleGameWindow)) {
        return false;
    }
    if (!host_.load_catalog(host_.assets_root() / "editor")) {
        host_.fatal().report("Failed to load the editor catalog (assets/editor/catalog.toml)");
        return false;
    }
    for (const std::string& arg : options.unknown) {
        engine::log::warn("Editor: unknown argument " + arg);
    }

    const engine::EngineServices& services = host_.services();
    world_ = &services.worlds.add();
    const std::optional<engine::WindowId> window = services.windows.open_window(kEditorWindow);
    if (!window) {
        host_.fatal().report("Failed to open the editor window");
        return false;
    }
    window_ = *window;
    services.worlds.bind_window(window_, *world_);
    services.worlds.enable_ui(*world_);

    toolbar_.emplace();
    cli_.emplace(*toolbar_);
    const engine::ecs::Entity canvas = world_->create();
    world_->emplace<engine::ui::UiCanvas>(canvas, engine::ui::UiCanvas{
            .document = assets::ui::editor,
            .stylesheet = assets::css::editor,
            .data_context = toolbar_->view_model(),
            .fit = engine::ui::UiFit::FillWindow,
            .window = window_,
    });
    panels_.emplace(*toolbar_);
    panels_->spawn(*world_, window_);
    world_->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [this](engine::ecs::World& world) { read_events(world); });
    // Game, not Bind: run_bind of this world (registered by enable_ui) must see this frame's copy.
    world_->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [this](engine::ecs::World& world) { panels_->frame(world); });

    const std::filesystem::path live = live_root();
    const std::size_t purged = engine::purge_game_module_copies(live);
    if (purged > 0) {
        engine::log::info("Editor: removed " + std::to_string(purged) + " stale game module copies");
    }
    play_host_.emplace(host_, *panels_);
    session_.emplace(services, *play_host_, live, kIdleGameWindow);
    build_.emplace(services.processes);
    toolbar_->show_state(RunState::Idle);

    sdk_root_ = sdk_root_of(host_.assets_root());
    if (auto manifest = engine::read_sdk_manifest(sdk_root_)) {
        sdk_ = std::move(*manifest);
        engine::log::info("Editor: SDK " + sdk_->version + " (" + sdk_->config + ") at " + path_text(sdk_root_));
    } else {
        engine::log::warn("Editor: not an installed SDK: " + engine::describe(manifest.error()));
    }

    open_project(*options.project);
    play_at_start_ = options.play;
    engine::log::info("Editor: started");
    return true;
}

void EditorApp::read_events(engine::ecs::World& world) {
    for (const engine::ui::WindowCloseRequestedEvent& event :
            engine::ecs::EventReader<engine::ui::WindowCloseRequestedEvent>{
                    world, world.ctx<engine::ecs::EventCursor<engine::ui::WindowCloseRequestedEvent>>()}) {
        if (event.window == window_) {
            close_requested_ = true;
        }
    }
}

// Every transition happens here, after the frame drew: Play and Stop destroy and create worlds, which no
// system of this frame may still be walking.
void EditorApp::on_frame_end() {
    engine::ApplicationState& app = host_.services().worlds.application_state();
    if (session_->playing() && !app.running && !quitting_) {
        // The game quit (its own menu, closing kPrimaryWindow, a fatal report). The editor stays.
        app.running = true;
        stop("The game quit. Stopped.");
    }
    if (close_requested_) {
        // on_quit stops a running game once the loop ends.
        quitting_ = true;
        app.quit();
        return;
    }
    // Before the toolbar's request, so a Play in the same frame builds the project just opened.
    if (const std::optional<std::filesystem::path> directory = cli_->take_open()) {
        open_project(*directory);
    }
    switch (toolbar_->take_request()) {
        case EditorRequest::None:
            break;
        case EditorRequest::Play:
            play();
            break;
        case EditorRequest::Stop:
            stop("Stopped.");
            break;
    }
    poll_build();
}

void EditorApp::on_quit() {
    if (build_) {
        build_->cancel();
    }
    if (session_ && session_->playing()) {
        stop("Stopped.");
    }
    engine::log::info("Editor: quit");
}

EditorFacts EditorApp::facts() const {
    return EditorFacts{
            .project = project_ ? project_->name : std::string{},
            .project_dir = project_dir_,
            .sdk = sdk_ ? sdk_->version : std::string{},
    };
}

void EditorApp::open_project(const std::filesystem::path& directory) {
    std::error_code error;
    const std::filesystem::path dir = std::filesystem::weakly_canonical(std::filesystem::absolute(directory), error);
    project_.reset();
    project_dir_ = error ? std::filesystem::absolute(directory) : dir;
    auto project = engine::read_wind_project(project_dir_);
    if (!project) {
        toolbar_->show_project(path_text(project_dir_), false);
        toolbar_->show_status(engine::describe(project.error()));
        engine::log::warn("Editor: " + engine::describe(project.error()));
        return;
    }
    const std::string line = project->name + "  (" + path_text(project_dir_) + ")";
    if (!sdk_) {
        toolbar_->show_project(line, false);
        toolbar_->show_status("This editor is not an installed SDK (no sdk.toml beside bin/): install it with "
                              "cmake --install to build projects.");
        return;
    }
    if (project->engine != sdk_->version) {
        toolbar_->show_project(line, false);
        toolbar_->show_status("The project needs engine " + project->engine + "; this editor is " + sdk_->version +
                ". Open it with that version.");
        return;
    }
    project_ = std::move(*project);
    toolbar_->show_project(line, true);
    toolbar_->show_status("Ready. Press Play.");
    engine::log::info("Editor: project " + project_->name + " at " + path_text(project_dir_));
}

void EditorApp::play() {
    if (!project_ || !sdk_ || session_->playing() || build_->running()) {
        return;
    }
    BuildPanel& log = panels_->build();
    log.clear();
    log.show_summary("Building " + project_->target + " (" + game_config(sdk_->config) + ")...");
    build_->start(BuildSetup{
            .project = project_dir_,
            .sdk = sdk_root_,
            .target = project_->target,
            .sdk_config = sdk_->config,
    });
    toolbar_->show_state(RunState::Building);
    toolbar_->show_status("Building " + project_->name + "...");
}

void EditorApp::poll_build() {
    std::vector<std::string> lines;
    const std::optional<BuildOutcome> outcome = build_->poll(lines);
    BuildPanel& log = panels_->build();
    log.append(std::move(lines));
    if (!outcome) {
        return;
    }
    if (!outcome->has_value()) {
        toolbar_->show_state(RunState::Idle);
        toolbar_->show_status(outcome->error());
        log.show_summary(log.first_error().empty() ? outcome->error() : log.first_error());
        toolbar_->show_build();
        engine::log::warn("Editor: " + outcome->error());
        return;
    }
    log.show_summary("Built " + path_text(**outcome) + ".");
    start_game(**outcome);
}

void EditorApp::start_game(const std::filesystem::path& module) {
    const auto started = session_->play(module);
    if (!started) {
        engine::log::warn("Editor: play failed: " + started.error());
        toolbar_->show_state(RunState::Idle);
        toolbar_->show_status(started.error());
        return;
    }
    toolbar_->show_state(RunState::Playing);
    std::string status = "Playing " + project_->name + ".";
    if (!started->empty()) {
        status += " " + *started;
    }
    toolbar_->show_status(std::move(status));
}

void EditorApp::stop(std::string status) {
    if (build_->running()) {
        build_->cancel();
        panels_->build().show_summary("Build cancelled.");
        toolbar_->show_state(RunState::Idle);
        toolbar_->show_status("Build cancelled.");
        return;
    }
    session_->stop();
    toolbar_->show_state(RunState::Idle);
    toolbar_->show_status(std::move(status));
}

}
