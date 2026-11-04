#include "core/server_presentation.h"

#include "core/shutdown_signal.h"

#include <engine/core/platform.h>
#include <engine/core/time.h>
#include <engine/log.h>

#include <cstdlib>
#include <format>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace engine {

namespace {

constexpr std::chrono::seconds kDroppedTickWarningInterval{10};

}

ServerPresentation::ServerPresentation() {
#if defined(_WIN32)
    timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
#endif
}

ServerPresentation::~ServerPresentation() {
#if defined(_WIN32)
    if (timer_ != nullptr) {
        CloseHandle(timer_);
    }
#endif
}

std::filesystem::path ServerPresentation::base_path() const {
    return executable_directory();
}

render::IGraphicFactory& ServerPresentation::factory() {
    std::abort();
}

render::IRenderBackend& ServerPresentation::backend() {
    std::abort();
}

render::ICanvas& ServerPresentation::canvas() {
    std::abort();
}

render::CommandBuffer& ServerPresentation::commands() {
    std::abort();
}

IWindowControl& ServerPresentation::window_control() {
    std::abort();
}

void ServerPresentation::poll(Worlds& worlds, InputSystem&) {
    if (shutdown_requested()) {
        if (!stop_logged_) {
            log::info("Stop signal received, shutting down");
            stop_logged_ = true;
        }
        worlds.application_state().quit();
    }
}

void ServerPresentation::sync_frame(Worlds& worlds) {
    // The first iteration's time includes on_start, which may load a world. Nobody was waiting for those ticks.
    if (first_frame_) {
        first_frame_ = false;
        return;
    }
    bool cap_hit = false;
    worlds.each_world([&](ecs::World& world) { cap_hit = cap_hit || worlds.pending_steps(world) >= kMaxFixedSteps; });
    if (!cap_hit) {
        return;
    }
    ++cap_hits_;
    const auto now = std::chrono::steady_clock::now();
    if (!last_warning_ || now - *last_warning_ >= kDroppedTickWarningInterval) {
        log::warn(std::format("The server fell behind and dropped ticks: {} stall(s) of {} ticks or more since the "
                              "last warning",
                cap_hits_, kMaxFixedSteps));
        cap_hits_ = 0;
        last_warning_ = now;
    }
}

void ServerPresentation::draw_all(std::span<FrameCapture>) {
    const std::chrono::nanoseconds wait = limiter_.wait_after(FrameLimiter::Clock::now(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(kFixed)));
    if (wait > std::chrono::nanoseconds::zero()) {
        sleep_for(wait);
    }
}

void ServerPresentation::sleep_for(std::chrono::nanoseconds duration) {
#if defined(_WIN32)
    if (timer_ != nullptr) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(duration.count() / 100);  // relative, in 100 ns
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer_, INFINITE);
            return;
        }
    }
#endif
    std::this_thread::sleep_for(duration);
}

}
