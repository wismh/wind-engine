#pragma once

// Headless EngineServices for tests that hand services to a game: no window, no GPU, no mixer device.
// Used by tests/game_module_test.cpp and editor/tests/play_session_test.cpp.

#include <engine/audio/audio_system.h>
#include <engine/core/engine_services.h>
#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/core/worlds.h>
#include <engine/haptics/haptics_system.h>
#include <engine/net/http_client.h>
#include <engine/process/process_launcher.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fakes {

class QuietFatal final : public engine::IFatalError {
public:
    void report(std::string_view) override {}
};

class FakeAudio final : public engine::IAudioSystem {
public:
    bool init() override {
        return true;
    }
    void dispose() override {}
    void update(float) override {}
    void play_sfx(const engine::Sound&, float) override {}
    void play_music(const engine::Sound&, bool, float) override {}
    void stop_music(float) override {}
    bool is_music_playing() const override {
        return false;
    }
    engine::LoopingSfxHandle create_looping_sfx() override {
        return {};
    }
    void play_looping_sfx(engine::LoopingSfxHandle, const engine::Sound&, float) override {}
    void stop_looping_sfx(engine::LoopingSfxHandle, float) override {}
    void release_looping_sfx(engine::LoopingSfxHandle, float) override {}
    void set_master_volume(float) override {}
    void set_music_volume(float) override {}
    void set_sfx_volume(float) override {}
    void stop_all() override {
        if (log != nullptr) {
            log->push_back("audio.stop_all");
        }
    }

    std::vector<std::string>* log = nullptr;
};

class FakeHaptics final : public engine::IHaptics {
public:
    bool init() override {
        return true;
    }
    void dispose() override {}
    void vibrate(float, float) override {}
    void cancel() override {}
    bool is_supported() const override {
        return false;
    }
};

class FakeHttp final : public engine::IHttpClient {
public:
    bool init() override {
        return true;
    }
    void dispose() override {}
    engine::HttpCall send(engine::HttpRequest) override {
        return engine::HttpCall::resolved(std::unexpected(engine::HttpError::Unsupported));
    }
    bool is_supported() const override {
        return false;
    }
};

class FakeProcesses final : public engine::IProcessLauncher {
public:
    void dispose() override {}
    engine::ProcessCall run(engine::ProcessDesc) override {
        return engine::ProcessCall::resolved(std::unexpected(engine::ProcessError::Unsupported));
    }
    std::expected<void, engine::ProcessError> launch(const engine::ProcessDesc&) override {
        return std::unexpected(engine::ProcessError::Unsupported);
    }
    bool is_supported() const override {
        return false;
    }
};

class FakeGraphicFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc&) override {
        return {};
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return {};
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc&) override {
        return {};
    }
};

class FakeBackend final : public engine::render::IRenderBackend {
public:
    void execute(const engine::render::CommandBuffer&) override {}
};

class FakeCanvas final : public engine::render::ICanvas {
public:
    void draw() override {}
};

// Keeps a list of live windows. kPrimaryWindow is open from the start. Every call that changes a
// window is appended to `log` when it is set.
class FakeWindowControl final : public engine::IWindowControl {
public:
    void set_overlay_mode(engine::OverlayMode mode) override {
        overlay = mode;
        note("windows.overlay");
    }
    engine::OverlayMode overlay_mode() const override {
        return overlay;
    }
    void set_vsync(bool enabled) override {
        vsync_enabled = enabled;
    }
    bool vsync() const override {
        return vsync_enabled;
    }
    void set_max_fps(int fps) override {
        max_frames_per_second = fps > 0 ? fps : 0;
    }
    int max_fps() const override {
        return max_frames_per_second;
    }
    void set_title(std::string_view text, engine::WindowId window) override {
        if (window == engine::kPrimaryWindow) {
            title = std::string(text);
        }
        note("windows.title " + std::string(text));
    }
    void set_borderless(bool value, engine::WindowId window) override {
        if (window == engine::kPrimaryWindow) {
            borderless = value;
        }
    }
    void set_always_on_top(bool value, engine::WindowId window) override {
        if (window == engine::kPrimaryWindow) {
            always_on_top = value;
        }
    }
    void set_position(glm::ivec2, engine::WindowId) override {}
    void resize(glm::ivec2 value, engine::WindowId window) override {
        if (window == engine::kPrimaryWindow) {
            primary_size = value;
        }
    }
    std::optional<glm::ivec2> position(engine::WindowId) const override {
        return std::nullopt;
    }
    std::optional<glm::ivec2> size(engine::WindowId) const override {
        return std::nullopt;
    }
    void set_click_through_enabled(bool, engine::WindowId) override {}
    void set_drag_region(std::optional<engine::render::Rect>, engine::WindowId) override {}
    std::optional<engine::WindowId> open_window(const engine::WindowDesc&) override {
        const engine::WindowId id{next_++};
        open.push_back(id);
        note("windows.open " + std::to_string(static_cast<std::uint32_t>(id)));
        return id;
    }
    void close_window(engine::WindowId id) override {
        std::erase(open, id);
        note("windows.close " + std::to_string(static_cast<std::uint32_t>(id)));
    }
    std::vector<engine::WindowId> open_windows() const override {
        return open;
    }
    engine::FileDialogCall request_open_file(engine::WindowId, std::vector<engine::FileFilter>) override {
        return engine::FileDialogCall::resolved(engine::FileDialogResult{});
    }
    engine::FileDialogCall request_open_folder(engine::WindowId, std::filesystem::path) override {
        return engine::FileDialogCall::resolved(engine::FileDialogResult{});
    }
    engine::render::Rect usable_display_bounds(int) const override {
        return {};
    }
    engine::render::Rect usable_display_bounds_for_window(engine::WindowId) const override {
        return {};
    }

    std::vector<engine::WindowId> open{engine::kPrimaryWindow};
    std::string title;
    glm::ivec2 primary_size{0, 0};
    bool borderless = false;
    bool always_on_top = false;
    engine::OverlayMode overlay = engine::OverlayMode::Auto;
    bool vsync_enabled = true;
    int max_frames_per_second = 0;
    std::vector<std::string>* log = nullptr;

private:
    void note(std::string line) {
        if (log != nullptr) {
            log->push_back(std::move(line));
        }
    }

    std::uint32_t next_ = 1;
};

// Owns one of everything EngineServices refers to.
struct Services {
    Services()
        : assets(fatal)
        , worlds(fatal)
        , services{
                  .assets = assets,
                  .input = input,
                  .audio = audio,
                  .haptics = haptics,
                  .http = http,
                  .processes = processes,
                  .windows = windows,
                  .graphics = graphics,
                  .backend = backend,
                  .canvas = canvas,
                  .commands = commands,
                  .worlds = worlds,
          } {}

    QuietFatal fatal;
    engine::AssetsDb assets;
    engine::InputSystem input;
    FakeAudio audio;
    FakeHaptics haptics;
    FakeHttp http;
    FakeProcesses processes;
    FakeWindowControl windows;
    FakeGraphicFactory graphics;
    FakeBackend backend;
    FakeCanvas canvas;
    engine::render::CommandBuffer commands;
    engine::Worlds worlds;
    engine::EngineServices services;
};

// Every world except `keep`, collected first so destroying does not disturb the walk.
inline void destroy_worlds_except(engine::Worlds& worlds, const engine::ecs::World& keep) {
    std::vector<engine::ecs::World*> doomed;
    worlds.each_world([&](engine::ecs::World& world) {
        if (&world != &keep) {
            doomed.push_back(&world);
        }
    });
    for (engine::ecs::World* world : doomed) {
        worlds.destroy(*world);
    }
}

}
