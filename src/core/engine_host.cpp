#include <engine/core/engine_host.h>

#include <engine/audio/audio_system.h>
#include <engine/builtin_ids.h>
#include <engine/core/engine_runtime.h>
#include <engine/core/input_system.h>
#include <engine/core/sdl_fatal_error.h>
#include <engine/core/worlds.h>
#include <engine/ecs/systems.h>
#include <engine/haptics/haptics_system.h>
#include <engine/igame.h>
#include <engine/log.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/font.h>
#include <engine/ui/canvas.h>

#include <optional>
#include <utility>

namespace engine {

struct EngineHost::Impl {
    EngineRuntime runtime;
    std::unique_ptr<SdlFatalError> fatal;
    std::unique_ptr<AssetsDb> assets;
    std::unique_ptr<InputSystem> input;
    std::unique_ptr<IAudioSystem> audio;
    std::unique_ptr<IHaptics> haptics;
    std::unique_ptr<Worlds> worlds;
    std::optional<EngineServices> services;
    bool initialized = false;
    bool opened = false;
};

EngineHost::EngineHost() : impl_(std::make_unique<Impl>()) {}

EngineHost::~EngineHost() {
    dispose();
}

bool EngineHost::init() {
    if (impl_->initialized) {
        return true;
    }
    EngineRuntime& runtime = impl_->runtime;
    if (!runtime.init_video()) {
        return false;
    }
    log::init(runtime.base_path());

    impl_->fatal = std::make_unique<SdlFatalError>();
    impl_->input = std::make_unique<InputSystem>();
    impl_->assets = std::make_unique<AssetsDb>(*impl_->fatal);
    impl_->audio = std::make_unique<AudioSystem>();
    impl_->haptics = std::make_unique<HapticsSystem>();
    impl_->worlds = std::make_unique<Worlds>(*impl_->fatal);
    impl_->services.emplace(EngineServices{
            .assets = *impl_->assets,
            .input = *impl_->input,
            .audio = *impl_->audio,
            .haptics = *impl_->haptics,
            .windows = runtime.window_control(),
            .graphics = runtime.factory(),
            .backend = runtime.backend(),
            .canvas = runtime.canvas(),
            .commands = runtime.commands(),
            .worlds = *impl_->worlds,
    });

    Worlds& worlds = *impl_->worlds;
    impl_->input->set_router([&worlds](WindowId id) { return worlds.world_for(id); });
    impl_->fatal->attach(worlds.application_state(), runtime.native_window());
    impl_->initialized = true;
    return true;
}

const EngineServices& EngineHost::services() const {
    return *impl_->services;
}

IFatalError& EngineHost::fatal() {
    return *impl_->fatal;
}

bool EngineHost::open_primary(const WindowDesc& desc) {
    if (!impl_->initialized) {
        return false;
    }
    if (impl_->opened) {
        return true;
    }
    EngineRuntime& runtime = impl_->runtime;
    if (!runtime.create_window(desc)) {
        dispose();
        return false;
    }
    impl_->fatal->attach(impl_->worlds->application_state(), runtime.native_window());

    if (!impl_->audio->init() || !impl_->haptics->init()) {
        dispose();
        return false;
    }

    AssetsDb& assets = *impl_->assets;
    assets.set_graphic_factory(&runtime.factory());
    const std::filesystem::path root = runtime.assets_root();
    if (root.empty()) {
        impl_->fatal->report("Assets root is missing");
        dispose();
        return false;
    }
    assets.set_root(root);
    if (!assets.load_catalog(root / "engine" / "catalog.toml", root / "engine")) {
        impl_->fatal->report("Failed to load engine catalog");
        dispose();
        return false;
    }
    // Only builtin::font_ui loads eagerly here — it's the fallback for any UI element with no
    // font-family at all. Every other font, and every UI image, is loaded lazily: run_ui_render
    // resolves what a drawn canvas actually references and calls ensure_ui_font/ensure_ui_image.
    if (!runtime.add_font_for_window(kPrimaryWindow, builtin::font_ui, *assets.get<Font>(builtin::font_ui))) {
        impl_->fatal->report("Failed to load UI font");
        dispose();
        return false;
    }

    // set_deps registers simulation systems on every world that exists already (a game constructed
    // before this call) and on every later Worlds::add. attach_game adds UI and audio.
    impl_->worlds->set_deps(EngineSystemDeps{
            .commands = &runtime.commands(),
            .fatal = impl_->fatal.get(),
            .assets = &assets,
            .audio = impl_->audio.get(),
            .commands_for_window = [&runtime](WindowId id) { return runtime.commands_for_window(id); },
            .ensure_ui_image = [&runtime, &assets](WindowId window, AssetId id) {
                (void)runtime.add_image_for_window(window, id, *assets.get<render::TextureDesc>(id));
            },
            .ensure_ui_font = [&runtime, &assets](WindowId window, AssetId id) {
                (void)runtime.add_font_for_window(window, id, *assets.get<Font>(id));
            },
    });
    impl_->opened = true;
    return true;
}

std::filesystem::path EngineHost::assets_root() const {
    return impl_->runtime.assets_root();
}

std::expected<void, MetaError> EngineHost::load_game_catalog(const std::filesystem::path& assets_dir) {
    const auto loaded = impl_->assets->load_catalog(assets_dir / "catalog.toml", assets_dir);
    if (!loaded && loaded.error() == MetaError::Io) {
        return {};
    }
    return loaded;
}

void EngineHost::unload_game_catalog(const std::filesystem::path& assets_dir) {
    impl_->assets->unload_catalog(assets_dir);
}

void EngineHost::attach_game(IGame& game) {
    EngineRuntime& runtime = impl_->runtime;
    Worlds& worlds = *impl_->worlds;
    if (const std::optional<AssetId> icon_id = game.window_icon(); icon_id.has_value()) {
        runtime.set_window_icon(*impl_->assets->get<render::TextureDesc>(*icon_id));
    }
    ecs::World& world = game.world();
    worlds.bind_window(kPrimaryWindow, world);
    worlds.enable_ui(world);
    worlds.enable_audio(world);
    runtime.write_window_size(worlds, true);
    ui::apply_canvas_fit(world);
}

int EngineHost::run(RunHooks hooks) {
    if (!impl_->opened) {
        return 1;
    }
    const int result = impl_->runtime.run(
            std::move(hooks), *impl_->worlds, *impl_->input, impl_->audio.get(), [this] { dispose(); });
    dispose();
    return result;
}

void EngineHost::dispose() {
    if (!impl_->initialized) {
        return;
    }
    impl_->audio->dispose();
    impl_->haptics->dispose();
    impl_->runtime.shutdown();
    impl_->initialized = false;
    impl_->opened = false;
}

}
