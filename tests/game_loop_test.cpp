#include <gtest/gtest.h>

#include "core/game_loop.h"
#include "core/presentation.h"
#include "fixtures/cli_client.h"

#include <engine/core/input_system.h>
#include <engine/core/run_hooks.h>
#include <engine/core/worlds.h>
#include <engine/resources/fatal_error.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
            worlds, input, nullptr, nullptr, nullptr, [&] { log.push_back("dispose"); });

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
            worlds, input, nullptr, nullptr, nullptr, {});

    EXPECT_EQ(result, 0);
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(frames, 3);
}

#if defined(ENGINE_CLI_SERVER)

TEST(GameLoop, CliReachesTheHostWithoutAPrimaryWorld) {
    QuietFatal fatal;
    engine::Worlds worlds{fatal};
    engine::InputSystem input;
    std::vector<std::string> log;
    FakePresentation presentation{log};

    // The editor between plays: no world on kPrimaryWindow, the host answers its own commands.
    cli_client::Descriptor descriptor;
    cli_client::Reply state;
    cli_client::Reply tree;
    std::atomic<bool> answered{false};
    std::thread client;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    engine::GameLoop loop;
    const int result = loop.run(presentation,
            engine::RunHooks{
                    .on_start = {},
                    .on_frame_end = [&] {
                        // The server starts after on_start, so the descriptor is there from the first frame.
                        if (!client.joinable()) {
                            descriptor = cli_client::read_descriptor();
                            client = std::thread([&] {
                                state = cli_client::post_authorized(descriptor, R"({"command":"state"})");
                                tree = cli_client::post_authorized(descriptor, R"({"command":"tree"})");
                                answered = true;
                            });
                        }
                        if (answered || std::chrono::steady_clock::now() > deadline) {
                            worlds.application_state().quit();
                        }
                    },
                    .on_quit = {},
                    .cli = engine::CliCommands{
                            .kind = "editor",
                            .handle = [](const engine::CliCommand& command) -> std::optional<engine::CliReply> {
                                if (command.name != "state") {
                                    return std::nullopt;
                                }
                                return engine::CliReply{
                                        .ok = true, .error = {}, .result = {{"run", std::string("idle")}}};
                            },
                    },
            },
            worlds, input, nullptr, nullptr, nullptr, {});
    client.join();

    EXPECT_EQ(result, 0);
    EXPECT_EQ(descriptor.kind, "editor");
    EXPECT_EQ(state.status, 200);
    EXPECT_EQ(state.body, R"({"ok":true,"result":{"run":"idle"}})");
    EXPECT_EQ(tree.body, R"({"ok":false,"error":"no world on window 0"})");
    EXPECT_TRUE(answered.load());
}

#endif
