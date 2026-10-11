#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW)

#include "core/presentation.h"

#include <engine/core/engine_error.h>
#include <engine/core/engine_host.h>

#include <cstdlib>
#include <memory>

namespace {

// A presentation whose video never starts. Everything past init_video aborts: EngineHost must not reach it.
class NoVideoPresentation final : public engine::IPresentation {
public:
    NoVideoPresentation(int& init_calls, int& create_calls)
        : init_calls_(&init_calls), create_calls_(&create_calls) {}

    bool init_video() override {
        ++*init_calls_;
        return false;
    }
    void shutdown() override {}
    bool create_primary(const engine::WindowDesc&) override {
        ++*create_calls_;
        return false;
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
        return false;
    }
    bool add_image(engine::WindowId, engine::AssetId, const engine::render::TextureDesc&) override {
        return false;
    }
    bool reset_ui_cache(engine::WindowId) override {
        return false;
    }
    void poll(engine::Worlds&, engine::InputSystem&) override {}
    void sync_frame(engine::Worlds&) override {}
    void draw_all(std::span<engine::FrameCapture>) override {}
    void attach_loop(engine::Worlds&, std::function<void()>) override {}
    void detach_loop(engine::Worlds&) override {}
    void publish_primary_size(engine::Worlds&, bool) override {}

private:
    int* init_calls_;
    int* create_calls_;
};

}

TEST(EngineHost, FailingVideoInitReturnsVideoInitWithoutAFatalReport) {
    int init_calls = 0;
    int create_calls = 0;
    // A fatal report would exit the test process: reaching the assertions proves none happened.
    engine::EngineHost host(std::make_unique<NoVideoPresentation>(init_calls, create_calls));

    const auto started = host.init();

    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), engine::EngineError::VideoInit);
    EXPECT_EQ(init_calls, 1);
}

TEST(EngineHost, OpenPrimaryAfterAFailedInitDoesNotCreateAWindow) {
    int init_calls = 0;
    int create_calls = 0;
    engine::EngineHost host(std::make_unique<NoVideoPresentation>(init_calls, create_calls));
    ASSERT_FALSE(host.init().has_value());

    const auto opened = host.open_primary(engine::WindowDesc{});

    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error(), engine::EngineError::VideoInit);
    EXPECT_EQ(create_calls, 0);
}

TEST(EngineHost, RunWithoutAnOpenedPrimaryReturnsOne) {
    int init_calls = 0;
    int create_calls = 0;
    engine::EngineHost host(std::make_unique<NoVideoPresentation>(init_calls, create_calls));
    ASSERT_FALSE(host.init().has_value());

    EXPECT_EQ(host.run(engine::RunHooks{}), 1);
}

#else

TEST(EngineHost, FailingVideoInitReturnsVideoInitWithoutAFatalReport) {
    GTEST_SKIP() << "EngineHost needs ENGINE_WITH_WINDOW";
}

#endif
