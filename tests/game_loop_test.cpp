#include <gtest/gtest.h>

#include "core/game_loop.h"
#include "core/presentation.h"

#include <engine/core/input_system.h>
#include <engine/core/run_hooks.h>
#include <engine/core/worlds.h>
#include <engine/resources/fatal_error.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

class QuietFatal final : public engine::IFatalError {
public:
    void report(std::string_view) override {}
};

// Records the calls GameLoop makes. Accessors the loop never reaches abort.
class FakePresentation final : public engine::IPresentation {
public:
    explicit FakePresentation(std::vector<std::string>& log) : log_(&log) {}

    bool init_video() override {
        return true;
    }
    void shutdown() override {}
    bool create_primary(const engine::WindowDesc&) override {
        return true;
    }
    void set_icon(const engine::render::TextureDesc&) override {}
    void* native_window() const override {
        return nullptr;
    }
    glm::ivec2 drawable_size() const override {
        return {};
    }
    std::filesystem::path base_path() const override {
        return {};
    }
    engine::render::IGraphicFactory& factory() override {
        std::abort();
    }
    engine::render::IRenderBackend& backend() override {
        std::abort();
    }
    engine::render::ICanvas& canvas() override {
        std::abort();
    }
    engine::render::CommandBuffer& commands() override {
        std::abort();
    }
    engine::render::CommandBuffer* commands_for(engine::WindowId) override {
        return nullptr;
    }
    engine::IWindowControl& window_control() override {
        std::abort();
    }
    bool add_font(engine::WindowId, engine::AssetId, const engine::Font&) override {
        return true;
    }
    bool add_image(engine::WindowId, engine::AssetId, const engine::render::TextureDesc&) override {
        return true;
    }
    bool reset_ui_cache(engine::WindowId) override {
        return true;
    }
    void poll(engine::Worlds&, engine::InputSystem&) override {
        log_->push_back("poll");
    }
    void sync_frame(engine::Worlds&) override {}
    void draw_all() override {
        log_->push_back("draw");
    }
    void attach_loop(engine::Worlds&, std::function<void()>) override {
        log_->push_back("attach");
    }
    void detach_loop(engine::Worlds&) override {
        log_->push_back("detach");
    }
    void publish_primary_size(engine::Worlds&, bool) override {}

private:
    std::vector<std::string>* log_;
};

}

TEST(GameLoop, RunHooksWrapTheFramesInOrder) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::InputSystem input;
    std::vector<std::string> log;
    FakePresentation presentation{log};
    int frames = 0;

    engine::GameLoop loop;
    const int result = loop.run(presentation,
            engine::RunHooks{
                    .on_start = [&] { log.push_back("start"); },
                    .on_frame_end = [&] {
                        log.push_back("frame_end");
                        if (++frames == 2) {
                            worlds.application_state().quit();
                        }
                    },
                    .on_quit = [&] { log.push_back("quit"); },
            },
            worlds, input, nullptr, [&] { log.push_back("dispose"); });

    EXPECT_EQ(result, 0);
    EXPECT_EQ(frames, 2);
    EXPECT_EQ(log, (std::vector<std::string>{"attach", "start", "poll", "draw", "frame_end", "poll", "draw",
                           "frame_end", "detach", "quit", "dispose"}));
}

TEST(GameLoop, FrameEndCanKeepTheLoopRunning) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::InputSystem input;
    std::vector<std::string> log;
    FakePresentation presentation{log};
    int frames = 0;
    int stops = 0;

    // The editor's Stop: a game quit is caught at frame end and undone, and the host decides.
    engine::GameLoop loop;
    const int result = loop.run(presentation,
            engine::RunHooks{
                    .on_start = {},
                    .on_frame_end = [&] {
                        ++frames;
                        engine::ApplicationState& app = worlds.application_state();
                        if (frames == 1) {
                            app.quit();
                        }
                        if (!app.running && stops == 0) {
                            ++stops;
                            app.running = true;
                        }
                        if (frames == 3) {
                            app.quit();
                        }
                    },
                    .on_quit = {},
            },
            worlds, input, nullptr, {});

    EXPECT_EQ(result, 0);
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(frames, 3);
}
