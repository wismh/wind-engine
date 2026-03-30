#pragma once

// docs/tech/modules/Core.md

#include <engine/core/input_system.h>
#include <engine/core/run_hooks.h>
#include <engine/core/window_control.h>
#include <engine/core/window_desc.h>
#include <engine/core/worlds.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/ecs/world.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>

#include <filesystem>
#include <functional>
#include <memory>

#include <glm/vec2.hpp>

namespace engine {

class IAudioSystem;
struct Font;

// Owns the windowed presentation and the game loop. SDL and OpenGL stay behind IPresentation.
class EngineRuntime {
public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    [[nodiscard]] bool init_video();
    [[nodiscard]] bool create_window(const WindowDesc& desc);
    void set_window_icon(const render::TextureDesc& desc);
    [[nodiscard]] bool add_font_for_window(WindowId id, AssetId asset, const Font& font);
    [[nodiscard]] bool add_image_for_window(WindowId id, AssetId asset, const render::TextureDesc& desc);
    // Drops the UI fonts and images registered for `id`. They are registered again when drawn.
    [[nodiscard]] bool reset_ui_cache(WindowId id);
    void shutdown();

    [[nodiscard]] int run(RunHooks hooks, Worlds& worlds, InputSystem& input, IAudioSystem* audio,
            std::function<void()> host_dispose = {});

    [[nodiscard]] render::CommandBuffer& commands();
    [[nodiscard]] render::ICanvas& canvas();
    [[nodiscard]] render::CommandBuffer* commands_for_window(WindowId id);
    [[nodiscard]] render::IGraphicFactory& factory();
    [[nodiscard]] render::IRenderBackend& backend();
    [[nodiscard]] IWindowControl& window_control();

    [[nodiscard]] void* native_window() const;
    [[nodiscard]] std::filesystem::path base_path() const;
    [[nodiscard]] std::filesystem::path assets_root() const;

    void write_window_size(Worlds& worlds, bool send_event);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
