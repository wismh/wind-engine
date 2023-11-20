#include "editor_app.h"

#include "project_check.h"
#include "project_export.h"

#include <asset_ids.h>

#include <engine/core/application_state.h>
#include <engine/core/platform.h>
#include <engine/core/window_control.h>
#include <engine/core/worlds.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
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

std::string editor_window_title(const std::optional<engine::SdkManifest>& sdk) {
    if (sdk && !sdk->version.empty()) {
        return "Wind Editor " + sdk->version;
    }
#if defined(WIND_ENGINE_VERSION)
    if (std::string_view(WIND_ENGINE_VERSION).size() > 0) {
        return "Wind Editor " + std::string(WIND_ENGINE_VERSION);
    }
#endif
    return "Wind Editor";
}

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

// `user_data_directory("Wind", "Editor")`, or empty when there is none (logged).
std::filesystem::path user_data() {
    const auto user_data = engine::user_data_directory("Wind", "Editor");
    if (user_data) {
        return *user_data;
    }
    engine::log::warn("Editor: no user data directory (" + user_data.error().message() +
            "). Game module copies go to the temp directory and the panel layout is not kept");
    return {};
}

// `<user data>/live`, where Play puts module copies. The temp directory when there is no user data
// directory, because Play cannot load the original in place.
std::filesystem::path live_root(const std::filesystem::path& user_data) {
    if (!user_data.empty()) {
        return user_data / "live";
    }
    return std::filesystem::temp_directory_path() / "wind_editor" / "live";
}

// `<user data>/dock_layout.toml`, the panel layout. None without a user data directory.
DockLayoutFile layout_file(const std::filesystem::path& user_data) {
    if (user_data.empty()) {
        return DockLayoutFile{};
    }
    return DockLayoutFile{user_data / "dock_layout.toml"};
}

}

EditorApp::~EditorApp() {
    session_.reset();
    panels_.reset();
    edit_module_.reset();
    window_ui_.reset();
}

int EditorApp::run(const EditorOptions& options) {
    if (!start(options)) {
        return 1;
    }
    return host_.run(engine::RunHooks{
            .on_start = [this] {
                if (play_at_start_) {
                    play_at_start_ = false;
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
    if (const auto started = host_.init(); !started) {
        engine::report_startup_error(started.error());
    }
    if (!options.project) {
        // The launcher passes --project; there is no Open dialog to fall back on.
        host_.fatal().report(kNoProjectMessage);
    }
    if (const auto opened = host_.open_primary(kIdleGameWindow); !opened) {
        engine::report_startup_error(opened.error());
    }
    if (!host_.load_catalog(host_.assets_root() / "editor")) {
        host_.fatal().report("Failed to load the editor catalog (assets/editor/catalog.toml)");
    }
    for (const std::string& arg : options.unknown) {
        engine::log::warn("Editor: unknown argument " + arg);
    }

    sdk_root_ = sdk_root_of(host_.assets_root());
    if (auto manifest = engine::read_sdk_manifest(sdk_root_)) {
        sdk_ = std::move(*manifest);
        engine::log::info("Editor: SDK " + sdk_->version + " (" + sdk_->config + ") at " + path_text(sdk_root_));
    } else {
        engine::log::warn("Editor: not an installed SDK: " + engine::describe(manifest.error()));
    }

    const engine::EngineServices& services = host_.services();
    world_ = &services.worlds.add();
    toolbar_.emplace();
    cli_.emplace(*toolbar_);
    engine::WindowDesc editor_window = kEditorWindow;
    editor_window.title = editor_window_title(sdk_);
    editor_window.style.borderless = true;
    toolbar_->view_model()->windowTitle = editor_window.title;
    auto window = engine::UiWindow::open(services.windows, services.worlds, *world_, editor_window,
            engine::ui::UiCanvas{
                    .document = assets::ui::editor,
                    .stylesheet = assets::css::editor,
                    .data_context = toolbar_->view_model(),
                    .fit = engine::ui::UiFit::FillWindow,
            });
    if (!window) {
        host_.fatal().report("Failed to open the editor window");
    }
    window_ui_.emplace(std::move(*window));
    window_ = window_ui_->id();
    toolbar_->set_window_control(services.windows, window_);
    const auto initial_win_size = services.windows.size(window_);
    if (initial_win_size) {
        window_ui_->set_drag_region(engine::render::Rect{0.0f, 0.0f, static_cast<float>(initial_win_size->x), 30.0f});
    }
    const std::filesystem::path data = user_data();
    panels_.emplace(layout_file(data));
    panels_->spawn(*world_, window_, services.windows);
    opening_overlay_.emplace();
    opening_overlay_->spawn(*world_, window_);
    // Game, not Bind: run_bind of this world (registered by enable_ui) must see this frame's copy.
    world_->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [this](engine::ecs::World& world) { panels_->frame(world); });
    world_->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Render,
            [this](engine::ecs::World& world) { panels_->render(world, host_.services().worlds.deps()); });

    const std::filesystem::path live = live_root(data);
    const std::size_t purged = engine::purge_game_module_copies(live);
    if (purged > 0) {
        engine::log::info("Editor: removed " + std::to_string(purged) + " stale game module copies");
    }
    play_host_.emplace(host_, *panels_);
    edit_module_.emplace(*play_host_, live);
    panels_->world_view().set_assets(&services.assets);
    panels_->world_view().set_component_registrar(
            [this](engine::ecs::WorldSerializer& s) { edit_module_->register_components(s); });
    session_.emplace(services, *play_host_, live, kIdleGameWindow);
    build_.emplace(services.processes);
    toolbar_->show_state(RunState::Idle);
    panels_->export_dialog().set_available(toolbar_->can_export_game());

    play_at_start_ = options.play;
    open_project(*options.project);
    engine::log::info("Editor: started");
    return true;
}

// Every transition happens here, after the frame drew: Play and Stop destroy and create worlds, which no
// system of this frame may still be walking.
void EditorApp::on_frame_end() {
    engine::ApplicationState& app = host_.services().worlds.application_state();
    if (session_->playing() && !app.running && !quitting_) {
        // The game quit (its own menu, closing kPrimaryWindow). The editor stays.
        app.running = true;
        stop("The game quit. Stopped.");
    }
    if (window_ui_->take_close_requested()) {
        // on_quit stops a running game once the loop ends.
        quitting_ = true;
        app.quit();
        return;
    }
    // Before the toolbar's request, so a Play in the same frame builds the project just opened.
    if (const std::optional<std::filesystem::path> directory = cli_->take_open()) {
        open_project(*directory);
    }
    if (opening_overlay_->take_skip_requested()) {
        if (opening_build_ && build_->running()) {
            build_->cancel();
        }
        opening_build_ = false;
        opening_overlay_->close();
        panels_->build().show_summary("Startup build skipped.");
        toolbar_->show_status("Ready. Press Play.");
    }
    ExportDialog& export_dialog = panels_->export_dialog();
    export_dialog.frame(host_.services().windows, window_);
    if (const std::optional<ExportChoice> choice = export_dialog.take_choice()) {
        start_export(*choice);
        panels_->close_export();
    }
    toolbar_->update_window_state();
    const auto current_win_size = host_.services().windows.size(window_);
    if (current_win_size && window_ui_) {
        window_ui_->set_drag_region(engine::render::Rect{0.0f, 0.0f, static_cast<float>(current_win_size->x), 30.0f});
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
        case EditorRequest::Export:
            export_game();
            break;
        case EditorRequest::Quit:
            host_.services().worlds.application_state().quit();
            break;
    }
    poll_build();
}

void EditorApp::on_quit() {
    quitting_ = true;
    build_->cancel();
    if (session_->playing()) {
        stop("Stopped.");
    }
    panels_->world_view().close();
    edit_module_->unload(world_);
    panels_->save_layout();
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
    if (build_->running()) {
        build_->cancel();
    }
    opening_build_ = false;
    opening_overlay_->close();
    panels_->world_view().close();
    edit_module_->reset(world_);
    project_.reset();
    panels_->close_export();
    ProjectCheck check = check_project(directory, sdk_);
    project_dir_ = check.directory;
    if (!check.project) {
        panels_->explorer().close();
        toolbar_->show_project("", false);
        panels_->export_dialog().set_available(false);
        toolbar_->show_status(check.problem);
        engine::log::warn("Editor: " + check.problem);
        return;
    }
    panels_->explorer().open(project_dir_);
    if (!check.fits) {
        toolbar_->show_project(check.project->name, false);
        panels_->export_dialog().set_available(false);
        toolbar_->show_status(check.problem);
        return;
    }
    project_ = std::move(check.project);
    toolbar_->show_project(project_->name, true);
    panels_->export_dialog().set_available(toolbar_->can_export_game());
    toolbar_->show_status("Ready. Press Play.");
    engine::log::info("Editor: project " + project_->name + " at " + path_text(project_dir_));
    if (!play_at_start_ && sdk_) {
        start_opening_build();
    }
}

void EditorApp::start_opening_build() {
    if (!project_ || !sdk_ || session_->playing() || build_->running()) {
        return;
    }
    building_ = BuildKind::Module;
    opening_build_ = true;
    opening_overlay_->open(project_->name, project_->target);
    BuildPanel& log = panels_->build();
    log.clear();
    log.show_summary("Building " + project_->target + " (" + game_config(sdk_->config, BuildKind::Module) + ")...");
    build_->start(BuildSetup{
            .kind = BuildKind::Module,
            .project = project_dir_,
            .sdk = sdk_root_,
            .target = project_->target,
            .sdk_config = sdk_->config,
    });
    toolbar_->show_status("Opening " + project_->name + "...");
}

void EditorApp::load_edit_module(const std::filesystem::path& module) {
    panels_->world_view().before_module_unload();
    if (const auto loaded = edit_module_->load(module); !loaded) {
        engine::log::warn("Editor: edit module load failed: " + loaded.error());
        toolbar_->show_status(loaded.error());
        return;
    }
    edit_module_->register_components(*world_);
    panels_->world_view().reload();
    toolbar_->show_status("Ready. Press Play.");
}

void EditorApp::play() {
    if (opening_build_ && build_->running()) {
        // Promote the running startup build into a Play build without restarting CMake.
        opening_build_ = false;
        opening_overlay_->close();
        toolbar_->show_state(RunState::Building);
        panels_->export_dialog().set_available(toolbar_->can_export_game());
        toolbar_->show_status("Building " + project_->name + "...");
        return;
    }
    start_build(BuildKind::Module);
}

// File -> Export opens the Export panel in a separate window.
void EditorApp::export_game() {
    if (project_ && sdk_) {
        panels_->show_export(project_dir_, project_->target, sdk_->config);
    }
}

void EditorApp::start_export(const ExportChoice& choice) {
    if (!project_ || !sdk_ || session_->playing() || build_->running()) {
        return;
    }
    export_directory_ = choice.directory;
    start_build(BuildKind::Export, choice.development);
}

// One operation at a time: not while the game runs, not while a build runs.
void EditorApp::start_build(BuildKind kind, bool development) {
    if (!project_ || !sdk_ || session_->playing() || build_->running()) {
        return;
    }
    building_ = kind;
    opening_build_ = false;
    opening_overlay_->close();
    BuildPanel& log = panels_->build();
    log.clear();
    log.show_summary("Building " + project_->target + " (" + game_config(sdk_->config, kind, development) + ")...");
    build_->start(BuildSetup{
            .kind = kind,
            .project = project_dir_,
            .sdk = sdk_root_,
            .target = project_->target,
            .sdk_config = sdk_->config,
            .development = development,
    });
    toolbar_->show_state(RunState::Building);
    panels_->export_dialog().set_available(toolbar_->can_export_game());
    toolbar_->show_status((kind == BuildKind::Export ? "Exporting " : "Building ") + project_->name + "...");
}

void EditorApp::poll_build() {
    std::vector<std::string> lines;
    const std::optional<BuildOutcome> outcome = build_->poll(lines);
    if (opening_build_ && !lines.empty()) {
        opening_overlay_->feed_lines(lines);
    }
    BuildPanel& log = panels_->build();
    log.append(std::move(lines));
    if (!outcome) {
        return;
    }
    const bool was_opening = std::exchange(opening_build_, false);
    if (was_opening) {
        opening_overlay_->close();
    }
    if (!outcome->has_value()) {
        toolbar_->show_state(RunState::Idle);
        panels_->export_dialog().set_available(toolbar_->can_export_game());
        toolbar_->show_status(outcome->error());
        log.show_summary(log.first_error().empty() ? outcome->error() : log.first_error());
        panels_->show(EditorPanels::kBuild);
        engine::log::warn("Editor: " + outcome->error());
        return;
    }
    log.show_summary("Built " + path_text(**outcome) + ".");
    if (was_opening) {
        load_edit_module(**outcome);
        return;
    }
    if (building_ == BuildKind::Export) {
        finish_export(**outcome);
        return;
    }
    start_game(**outcome);
}

// The build ended: copy what it made out of the build directory.
void EditorApp::finish_export(const std::filesystem::path& built) {
    BuildPanel& log = panels_->build();
    toolbar_->show_state(RunState::Idle);
    panels_->export_dialog().set_available(toolbar_->can_export_game());
    const auto copied = copy_export(built, export_directory_);
    if (!copied) {
        toolbar_->show_status(copied.error());
        log.show_summary(copied.error());
        panels_->show(EditorPanels::kBuild);
        engine::log::warn("Editor: " + copied.error());
        return;
    }
    const std::string status = "Exported to " + path_text(*copied);
    toolbar_->show_status(status);
    log.show_summary(status);
    engine::log::info("Editor: " + status);
}

void EditorApp::start_game(const std::filesystem::path& module) {
    edit_module_->remember_module(module);
    panels_->world_view().before_module_unload();
    edit_module_->unload(world_);
    const auto started = session_->play(module);
    if (!started) {
        engine::log::warn("Editor: play failed: " + started.error());
        if (edit_module_->load(module)) {
            edit_module_->register_components(*world_);
            panels_->world_view().reload();
        }
        toolbar_->show_state(RunState::Idle);
        panels_->export_dialog().set_available(toolbar_->can_export_game());
        toolbar_->show_status(started.error());
        return;
    }
    toolbar_->show_state(RunState::Playing);
    panels_->export_dialog().set_available(toolbar_->can_export_game());
    std::string status = "Playing " + project_->name + ".";
    if (!started->empty()) {
        status += " " + *started;
    }
    toolbar_->show_status(std::move(status));
}

void EditorApp::stop(std::string status) {
    if (build_->running()) {
        build_->cancel();
        opening_build_ = false;
        opening_overlay_->close();
        panels_->build().show_summary("Build cancelled.");
        toolbar_->show_state(RunState::Idle);
        panels_->export_dialog().set_available(toolbar_->can_export_game());
        toolbar_->show_status("Build cancelled.");
        return;
    }
    session_->stop();
    if (!quitting_ && !edit_module_->module_path().empty()) {
        if (const auto loaded = edit_module_->load(edit_module_->module_path())) {
            edit_module_->register_components(*world_);
            panels_->world_view().reload();
        } else {
            engine::log::warn("Editor: edit module reload failed: " + loaded.error());
        }
    }
    toolbar_->show_state(RunState::Idle);
    panels_->export_dialog().set_available(toolbar_->can_export_game());
    toolbar_->show_status(std::move(status));
}

}
