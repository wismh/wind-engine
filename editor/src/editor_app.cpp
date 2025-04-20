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

#include <utility>
#include <vector>

namespace editor {
namespace {

// kPrimaryWindow between plays. Play applies the game's own description; Stop puts title and style back.
const engine::WindowDesc kIdleGameWindow{.title = "Game", .size = {800, 600}};
const engine::WindowDesc kEditorWindow{.title = "Wind Editor", .size = {1280, 800}};

#if defined(_WIN32)
constexpr char kModulePattern[] = "dll";
#elif defined(__APPLE__)
constexpr char kModulePattern[] = "dylib";
#else
constexpr char kModulePattern[] = "so";
#endif

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
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
    });
}

bool EditorApp::start(const EditorOptions& options) {
    if (!host_.init()) {
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
    const engine::ecs::Entity canvas = world_->create();
    world_->emplace<engine::ui::UiCanvas>(canvas, engine::ui::UiCanvas{
            .document = assets::ui::editor,
            .stylesheet = assets::css::editor,
            .data_context = toolbar_->view_model(),
            .fit = engine::ui::UiFit::FillWindow,
            .window = window_,
    });
    world_->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
            [this](engine::ecs::World& world) { read_events(world); });

    const std::filesystem::path live = live_root();
    const std::size_t purged = engine::purge_game_module_copies(live);
    if (purged > 0) {
        engine::log::info("Editor: removed " + std::to_string(purged) + " stale game module copies");
    }
    play_host_.emplace(host_);
    session_.emplace(services, *play_host_, live, kIdleGameWindow);
    toolbar_->show_playing(false);

    if (options.game) {
        game_path_ = std::filesystem::absolute(*options.game);
        toolbar_->show_game(game_path_);
        toolbar_->show_status("Ready. Press Play.");
        play_at_start_ = options.play;
    } else {
        toolbar_->show_status("Choose a game module.");
        choose_game();
    }
    engine::log::info("Editor: started");
    return true;
}

void EditorApp::read_events(engine::ecs::World& world) {
    for (const engine::FileDialogResultEvent& event : engine::ecs::EventReader<engine::FileDialogResultEvent>{
                 world, world.ctx<engine::ecs::EventCursor<engine::FileDialogResultEvent>>()}) {
        if (dialog_ && event.request == *dialog_) {
            dialog_.reset();
            dialog_answer_ = event.path;
        }
    }
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
    if (dialog_answer_) {
        const std::optional<std::filesystem::path> answer = std::move(*dialog_answer_);
        dialog_answer_.reset();
        take_dialog_answer(answer);
    }
    switch (toolbar_->take_request()) {
        case EditorRequest::None:
            break;
        case EditorRequest::ChooseGame:
            choose_game();
            break;
        case EditorRequest::Play:
            play();
            break;
        case EditorRequest::Stop:
            stop("Stopped.");
            break;
    }
}

void EditorApp::on_quit() {
    if (session_ && session_->playing()) {
        stop("Stopped.");
    }
    engine::log::info("Editor: quit");
}

void EditorApp::choose_game() {
    if (dialog_) {
        return;
    }
    std::vector<engine::FileFilter> filters{
            engine::FileFilter{.name = "Wind game module", .pattern = kModulePattern},
    };
    dialog_ = host_.services().windows.request_open_file(window_, std::move(filters));
}

void EditorApp::take_dialog_answer(const std::optional<std::filesystem::path>& path) {
    if (!path) {
        if (game_path_.empty()) {
            toolbar_->show_status("No game chosen. Press Choose game.");
        }
        return;
    }
    game_path_ = *path;
    toolbar_->show_game(game_path_);
    toolbar_->show_status("Ready. Press Play.");
    engine::log::info("Editor: game " + path_text(game_path_));
}

void EditorApp::play() {
    if (game_path_.empty() || session_->playing()) {
        return;
    }
    const auto started = session_->play(game_path_);
    if (!started) {
        engine::log::warn("Editor: play failed: " + started.error());
        toolbar_->show_status(started.error());
        return;
    }
    toolbar_->show_playing(true);
    std::string status = "Playing " + path_text(game_path_.filename()) + ".";
    if (!started->empty()) {
        status += " " + *started;
    }
    toolbar_->show_status(std::move(status));
}

void EditorApp::stop(std::string status) {
    session_->stop();
    toolbar_->show_playing(false);
    toolbar_->show_status(std::move(status));
}

}
