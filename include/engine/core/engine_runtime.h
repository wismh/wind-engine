#pragma once

#include <engine/core/application_state.h>
#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/core/window_desc.h>
#include <engine/igame.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <glm/vec2.hpp>

namespace engine {

class IAudioSystem;
struct Font;

class EngineRuntime {
public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    [[nodiscard]] bool init_video();
    [[nodiscard]] bool create_window(const WindowDesc& desc);
    // Secondary windows (SDD §21.5): share GL resources with the primary window, get drawn/swapped
    // every frame alongside it. No per-window UI/render routing yet (§21.6) — a freshly opened
    // window is just cleared each frame until that phase lands.
    [[nodiscard]] std::optional<WindowId> open_window(const WindowDesc& desc);
    void close_window(WindowId id);
    void set_window_icon(const render::TextureDesc& desc);
    [[nodiscard]] bool load_ui_font(const Font& font);
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool add_image(AssetId id, const render::TextureDesc& desc);
    void shutdown();

    [[nodiscard]] int run(IGame& game, InputSystem& input, IAudioSystem* audio,
            std::function<void()> host_dispose = {});

    [[nodiscard]] render::CommandBuffer& commands();
    [[nodiscard]] render::ICanvas& canvas();
    // Additive (§21.6): the CommandBuffer for any live window, primary or secondary — nullptr if
    // `id` has no live window. Exposed as a plain WindowId -> CommandBuffer* lookup (rather than
    // leaking WindowManager, which stays src-private) so Engine<GameT>::init() can wire
    // EngineSystemDeps::commands_for_window without widening this header's dependencies.
    [[nodiscard]] render::CommandBuffer* commands_for_window(WindowId id);
    [[nodiscard]] render::IGraphicFactory& factory();
    [[nodiscard]] render::IRenderBackend& backend();

    [[nodiscard]] std::shared_ptr<render::CommandBuffer> commands_ptr() const;
    [[nodiscard]] std::shared_ptr<render::ICanvas> canvas_ptr() const;
    [[nodiscard]] std::shared_ptr<render::IGraphicFactory> factory_ptr() const;
    [[nodiscard]] std::shared_ptr<render::IRenderBackend> backend_ptr() const;
    [[nodiscard]] std::shared_ptr<IWindowControl> window_control_ptr() const;

    [[nodiscard]] void* native_window() const;
    [[nodiscard]] glm::ivec2 drawable_size() const;
    [[nodiscard]] std::filesystem::path base_path() const;
    [[nodiscard]] std::filesystem::path assets_root() const;

    void write_window_size(ecs::World& world, bool send_event);

private:
    void poll_events(ecs::World& world, InputSystem& input, ApplicationState& app);
    void begin_loop(IGame& game, InputSystem& input, IAudioSystem* audio);
    void tick_loop();
    void end_loop();
    static void main_loop_thunk(void* self);

    // Windows-only (SDD §21.7/wind-89): registered with WindowManager as its modal-loop tick
    // callback for as long as the loop is running (begin_loop()..end_loop()), so game logic and
    // rendering keep advancing in real time while the user is dragging/resizing a window, not just
    // redrawing the last frame. See its .cpp doc comment for why this is safe to call reentrantly
    // from inside SDL_PollEvent() specifically (it deliberately never touches
    // world.flush_events()/poll_events() — those stay tick_loop()-only).
    void reentrant_tick();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
