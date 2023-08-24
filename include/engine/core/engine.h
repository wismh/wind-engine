#pragma once

#if !defined(ENGINE_WITH_WINDOW)
#error "engine::Engine requires ENGINE_WITH_WINDOW"
#endif

#include <engine/audio/audio_system.h>
#include <engine/builtin_ids.h>
#include <engine/core/application_state.h>
#include <engine/core/engine_runtime.h>
#include <engine/core/input_system.h>
#include <engine/core/sdl_fatal_error.h>
#include <engine/ecs/systems.h>
#include <engine/igame.h>
#include <engine/log.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/font.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <boost/di.hpp>

#include <concepts>
#include <filesystem>
#include <memory>
#include <utility>

namespace engine {

namespace di = boost::di;

template<typename GameT>
    requires std::derived_from<GameT, IGame>
class Engine {
public:
    Engine() = default;
    ~Engine() {
        dispose();
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init();
    int run();
    void dispose();

private:
    EngineRuntime runtime_;
    std::shared_ptr<IFatalError> fatal_;
    std::shared_ptr<AssetsDb> assets_;
    std::shared_ptr<InputSystem> input_;
    std::shared_ptr<IAudioSystem> audio_;
    std::shared_ptr<IGame> game_;
    bool initialized_ = false;
};

template<typename GameT>
    requires std::derived_from<GameT, IGame>
bool Engine<GameT>::init() {
    if (initialized_) {
        return true;
    }
    if (!runtime_.init_video()) {
        return false;
    }
    log::init(runtime_.base_path());

    input_ = std::make_shared<InputSystem>();
    auto injector = di::make_injector(
            di::bind<IFatalError>().to<SdlFatalError>().in(di::singleton),
            di::bind<AssetsDb>().in(di::singleton),
            di::bind<InputSystem>().to(input_),
            di::bind<IAudioSystem>().to<AudioSystem>().in(di::singleton),
            di::bind<render::CommandBuffer>().to(runtime_.commands_ptr()),
            di::bind<render::ICanvas>().to(runtime_.canvas_ptr()),
            di::bind<render::IGraphicFactory>().to(runtime_.factory_ptr()),
            di::bind<render::IRenderBackend>().to(runtime_.backend_ptr()),
            di::bind<IGame>().to<GameT>().in(di::singleton));

    fatal_ = injector.template create<std::shared_ptr<IFatalError>>();
    assets_ = injector.template create<std::shared_ptr<AssetsDb>>();
    audio_ = injector.template create<std::shared_ptr<IAudioSystem>>();
    game_ = injector.template create<std::shared_ptr<IGame>>();
    if (!fatal_ || !assets_ || !audio_ || !game_) {
        runtime_.shutdown();
        return false;
    }

    input_->set_world(game_->world());
    if (auto* sdl_fatal = dynamic_cast<SdlFatalError*>(fatal_.get())) {
        sdl_fatal->attach(game_->world().ctx<ApplicationState>(), runtime_.native_window());
    }

    if (!runtime_.create_window(game_->window_title(), game_->window_size())) {
        runtime_.shutdown();
        return false;
    }
    if (auto* sdl_fatal = dynamic_cast<SdlFatalError*>(fatal_.get())) {
        sdl_fatal->attach(game_->world().ctx<ApplicationState>(), runtime_.native_window());
    }

    if (!audio_->init()) {
        runtime_.shutdown();
        return false;
    }

    assets_->set_graphic_factory(&runtime_.factory());
    const std::filesystem::path root = runtime_.assets_root();
    if (root.empty()) {
        fatal_->report("Assets root is missing");
        runtime_.shutdown();
        return false;
    }
    assets_->set_root(root);
    if (!assets_->load_catalog(root / "engine" / "catalog.toml", root / "engine")) {
        fatal_->report("Failed to load engine catalog");
        runtime_.shutdown();
        return false;
    }
    if (const auto loaded = assets_->load_catalog(root / "catalog.toml", root); !loaded) {
        if (loaded.error() != MetaError::Io) {
            fatal_->report("Failed to load game catalog");
            runtime_.shutdown();
            return false;
        }
    }
    if (!runtime_.load_ui_font(*assets_->get<Font>(builtin::font_ui))) {
        fatal_->report("Failed to load UI font");
        runtime_.shutdown();
        return false;
    }
    for (const CatalogEntry& entry : assets_->catalog().entries()) {
        if (entry.importer != ImporterKind::Font || entry.guid == builtin::font_ui) {
            continue;
        }
        if (!runtime_.add_font(entry.guid, *assets_->get<Font>(entry.guid))) {
            fatal_->report("Failed to load UI font");
            runtime_.shutdown();
            return false;
        }
    }

    runtime_.write_window_size(game_->world(), true);
    register_engine_systems(game_->world(), EngineSystemDeps{
            .commands = &runtime_.commands(),
            .fatal = fatal_.get(),
            .assets = assets_.get(),
            .audio = audio_.get(),
    });
    ui::apply_fill_window(game_->world());

    initialized_ = true;
    return true;
}

template<typename GameT>
    requires std::derived_from<GameT, IGame>
int Engine<GameT>::run() {
    if (!initialized_ || !game_ || !input_) {
        return 1;
    }
    const int result = runtime_.run(*game_, *input_, audio_.get());
    dispose();
    return result;
}

template<typename GameT>
    requires std::derived_from<GameT, IGame>
void Engine<GameT>::dispose() {
    if (!initialized_) {
        return;
    }
    if (audio_) {
        audio_->dispose();
    }
    runtime_.shutdown();
    initialized_ = false;
}

}
