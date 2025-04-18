#pragma once

#include "play_host.h"

namespace engine {
class EngineHost;
}

namespace editor {

// IPlayHost over the editor's EngineHost.
class EngineHostPlay final : public IPlayHost {
public:
    explicit EngineHostPlay(engine::EngineHost& host);

    [[nodiscard]] std::expected<void, std::string> load_catalog(const std::filesystem::path& assets_dir) override;
    void unload_catalog(const std::filesystem::path& assets_dir) override;
    void attach(engine::IGame& game) override;
    void detach() override;

private:
    engine::EngineHost* host_;
};

}
