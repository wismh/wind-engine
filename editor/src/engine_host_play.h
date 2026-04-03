#pragma once

#include "play_host.h"

namespace engine {
class EngineHost;
}

namespace editor {

class EditorPanels;

// IPlayHost over the editor's EngineHost and its panels.
class EngineHostPlay final : public IPlayHost {
public:
    EngineHostPlay(engine::EngineHost& host, EditorPanels& panels);

    [[nodiscard]] std::expected<void, std::string> load_catalog(const std::filesystem::path& assets_dir) override;
    void unload_catalog(const std::filesystem::path& assets_dir) override;
    void attach(engine::IGame& game) override;
    void detach() override;
    void attach_tools(engine::ecs::World& game_world) override;
    void detach_tools() override;

private:
    engine::EngineHost* host_;
    EditorPanels* panels_;
};

}
