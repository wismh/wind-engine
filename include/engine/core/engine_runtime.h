#pragma once

#include <engine/core/application_state.h>
#include <engine/core/input_system.h>
#include <engine/igame.h>
#include <engine/render/backend.h>
#include <engine/render/canvas.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphic_factory.h>

#include <filesystem>
#include <memory>
#include <string_view>

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
    [[nodiscard]] bool create_window(std::string_view title, glm::ivec2 size);
    [[nodiscard]] bool load_ui_font(const Font& font);
    void shutdown();

    [[nodiscard]] int run(IGame& game, InputSystem& input, IAudioSystem* audio);

    [[nodiscard]] render::CommandBuffer& commands();
    [[nodiscard]] render::ICanvas& canvas();
    [[nodiscard]] render::IGraphicFactory& factory();
    [[nodiscard]] render::IRenderBackend& backend();

    [[nodiscard]] std::shared_ptr<render::CommandBuffer> commands_ptr() const;
    [[nodiscard]] std::shared_ptr<render::ICanvas> canvas_ptr() const;
    [[nodiscard]] std::shared_ptr<render::IGraphicFactory> factory_ptr() const;
    [[nodiscard]] std::shared_ptr<render::IRenderBackend> backend_ptr() const;

    [[nodiscard]] void* native_window() const;
    [[nodiscard]] glm::ivec2 drawable_size() const;
    [[nodiscard]] std::filesystem::path assets_root() const;

    void write_window_size(ecs::World& world, bool send_event);

private:
    void poll_events(ecs::World& world, InputSystem& input, ApplicationState& app);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
