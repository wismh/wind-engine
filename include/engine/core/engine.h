#pragma once

#if !defined(ENGINE_WITH_WINDOW)
#error "engine::Engine requires ENGINE_WITH_WINDOW"
#endif

#include <engine/audio/audio_system.h>
#include <engine/builtin_ids.h>
#include <engine/core/application_state.h>
#include <engine/core/engine_runtime.h>
#include <engine/core/engine_services.h>
#include <engine/core/input_system.h>
#include <engine/core/sdl_fatal_error.h>
#include <engine/ecs/systems.h>
#include <engine/haptics/haptics_system.h>
#include <engine/igame.h>
#include <engine/log.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/font.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>

#include <concepts>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace engine {

template<typename GameT>
concept EngineGame = std::derived_from<GameT, IGame> && std::constructible_from<GameT, const EngineServices&>;

template<EngineGame GameT>
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
    std::shared_ptr<IHaptics> haptics_;
    std::shared_ptr<IGame> game_;
    bool initialized_ = false;
};

template<EngineGame GameT>
bool Engine<GameT>::init() {
    if (initialized_) {
        return true;
    }
    if (!runtime_.init_video()) {
        return false;
    }
    log::init(runtime_.base_path());

    auto sdl_fatal = std::make_shared<SdlFatalError>();
    fatal_ = sdl_fatal;
    input_ = std::make_shared<InputSystem>();
    assets_ = std::make_shared<AssetsDb>(*fatal_);
    audio_ = std::make_shared<AudioSystem>();
    haptics_ = std::make_shared<HapticsSystem>();
    const EngineServices services{
            .assets = *assets_,
            .input = *input_,
            .audio = *audio_,
            .haptics = *haptics_,
            .windows = runtime_.window_control(),
            .graphics = runtime_.factory(),
            .backend = runtime_.backend(),
            .canvas = runtime_.canvas(),
            .commands = runtime_.commands(),
    };
    game_ = std::make_shared<GameT>(services);

    input_->set_world(game_->world());
    sdl_fatal->attach(game_->world().ctx<ApplicationState>(), runtime_.native_window());

    if (!runtime_.create_window(game_->primary_window())) {
        runtime_.shutdown();
        return false;
    }
    sdl_fatal->attach(game_->world().ctx<ApplicationState>(), runtime_.native_window());

    if (!audio_->init()) {
        runtime_.shutdown();
        return false;
    }
    if (!haptics_->init()) {
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
    // Only builtin::font_ui loads eagerly here — it's the fallback for any UI element with no
    // font-family at all. Every other font, and every UI image, is loaded lazily: run_ui_render
    // resolves what a drawn canvas actually references and calls ensure_ui_font/ensure_ui_image.
    if (!runtime_.add_font_for_window(kPrimaryWindow, builtin::font_ui, *assets_->get<Font>(builtin::font_ui))) {
        fatal_->report("Failed to load UI font");
        runtime_.shutdown();
        return false;
    }

    if (const std::optional<AssetId> icon_id = game_->window_icon(); icon_id.has_value()) {
        runtime_.set_window_icon(*assets_->get<render::TextureDesc>(*icon_id));
    }

    runtime_.write_window_size(game_->world(), true);
    register_engine_systems(game_->world(), EngineSystemDeps{
            .commands = &runtime_.commands(),
            .fatal = fatal_.get(),
            .assets = assets_.get(),
            .audio = audio_.get(),
            .commands_for_window = [this](WindowId id) { return runtime_.commands_for_window(id); },
            .ensure_ui_image = [this](WindowId window, AssetId id) {
                (void)runtime_.add_image_for_window(window, id, *assets_->get<render::TextureDesc>(id));
            },
            .ensure_ui_font = [this](WindowId window, AssetId id) {
                (void)runtime_.add_font_for_window(window, id, *assets_->get<Font>(id));
            },
    });
    ui::apply_canvas_fit(game_->world());

    initialized_ = true;
    return true;
}

template<EngineGame GameT>
int Engine<GameT>::run() {
    if (!initialized_ || !game_ || !input_) {
        return 1;
    }
    const int result = runtime_.run(*game_, *input_, audio_.get(), [this] { dispose(); });
    dispose();
    return result;
}

template<EngineGame GameT>
void Engine<GameT>::dispose() {
    if (!initialized_) {
        return;
    }
    if (audio_) {
        audio_->dispose();
    }
    if (haptics_) {
        haptics_->dispose();
    }
    runtime_.shutdown();
    initialized_ = false;
}

}
