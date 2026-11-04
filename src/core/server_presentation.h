#pragma once

#include "core/frame_limiter.h"
#include "core/presentation.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace engine {

// The `IPresentation` of a dedicated server: no window and no drawing. `GameLoop` runs on it unchanged, so a server has
// the same frame order as a game. `poll` turns a stop signal into `ApplicationState::quit`, `sync_frame` reports
// dropped ticks, and `draw_all` is where the tick waits, as it is in a window (with no window to swap, on `kFixed`).
// The accessors `GameLoop` never reaches abort.
class ServerPresentation final : public IPresentation {
public:
    ServerPresentation();
    ~ServerPresentation() override;

    [[nodiscard]] bool init_video() override {
        return true;
    }
    void shutdown() override {}
    [[nodiscard]] bool create_primary(const WindowDesc&) override {
        return false;
    }
    void set_icon(const render::TextureDesc&) override {}
    [[nodiscard]] void* native_window() const override {
        return nullptr;
    }
    [[nodiscard]] glm::ivec2 drawable_size() const override {
        return {};
    }
    [[nodiscard]] std::filesystem::path base_path() const override;

    [[nodiscard]] render::IGraphicFactory& factory() override;
    [[nodiscard]] render::IRenderBackend& backend() override;
    [[nodiscard]] render::ICanvas& canvas() override;
    [[nodiscard]] render::CommandBuffer& commands() override;
    [[nodiscard]] render::CommandBuffer* commands_for(WindowId) override {
        return nullptr;
    }
    [[nodiscard]] IWindowControl& window_control() override;

    [[nodiscard]] bool add_font(WindowId, AssetId, const Font&) override {
        return false;
    }
    [[nodiscard]] bool add_image(WindowId, AssetId, const render::TextureDesc&) override {
        return false;
    }
    [[nodiscard]] bool reset_ui_cache(WindowId) override {
        return false;
    }

    void poll(Worlds& worlds, InputSystem& input) override;
    void sync_frame(Worlds& worlds) override;
    void draw_all(std::span<FrameCapture> captures) override;

    void attach_loop(Worlds&, std::function<void()>) override {}
    void detach_loop(Worlds&) override {}
    void publish_primary_size(Worlds&, bool) override {}

private:
    void sleep_for(std::chrono::nanoseconds duration);

    FrameLimiter limiter_;
    bool stop_logged_ = false;
    bool first_frame_ = true;
    // Iterations that hit `kMaxFixedSteps`, where `FixedStepClock` drops what is left over, since the last warning.
    std::uint64_t cap_hits_ = 0;
    std::optional<std::chrono::steady_clock::time_point> last_warning_;
#if defined(_WIN32)
    // A high-resolution waitable timer. The default system timer ticks every 15.6 ms, about one whole server tick.
    void* timer_ = nullptr;
#endif
};

}
