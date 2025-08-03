#pragma once

// docs/tech/features/CLI.md

#include "core/frame_capture.h"

#include <engine/core/cli_commands.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>
#include <engine/render/commands.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::cli {

    struct CliRequest {
        std::string command;
        std::string selector;
        std::uint32_t window = 0;
        double x = 0.0;
        double y = 0.0;
        bool has_x = false;
        bool has_y = false;
        bool stop = false;
        // `open`: the project directory, passed to the host as `CliCommand::path`. `screenshot`: the PNG to write.
        // Both are absolute, UTF-8.
        std::string path;
        std::string error;
    };

    struct CliResponse {
        std::string json;
        // Profile is waiting for a frame committed while capture is on. The socket thread keeps
        // the job; the next drain calls execute again.
        bool pending = false;
    };

    // What a frame offers the server: the world bound to each window, the host's own commands, and the windows
    // this frame read back for an armed `screenshot` (capture_requests).
    struct CliFrame {
        std::function<ecs::World *(WindowId)> world_for;
        const CliCommands *host = nullptr;
        std::span<const FrameCapture> captures;
    };

    [[nodiscard]] CliRequest parse_request(std::string_view body);

    // `{"ok":false,"error":message}`.
    [[nodiscard]] std::string error_json(std::string_view message);

    // `tree`, `element`, `hit`, `click`, and `profile`: answered from the world bound to the request's window.
    // `screenshot` is not one: it reads the window's pixels and needs a world only for a selector.
    [[nodiscard]] bool is_ui_command(std::string_view command);

    // The border box, in window pixels, of the one element `request.selector` names on the request's window: the
    // canvas's layout box mapped through its offset and scale. Otherwise the error body `element` would answer.
    [[nodiscard]] std::expected<render::Rect, std::string> element_window_rect(ecs::World &world,
                                                                              const CliRequest &request);

    // Any other command: the host's reply, or `unknown command` when there is no host or it does not know it.
    [[nodiscard]] std::string execute_host(const CliCommands *host, const CliRequest &request);

    // Runs the command on this thread. Click runs immediately; the socket server defers that
    // call to the start of the next frame and answers after that frame's draw.
    [[nodiscard]] CliResponse execute(ecs::World &world, const CliRequest &request);

#if defined(ENGINE_CLI_SERVER)
    // Loopback server. No-op when bind or the descriptor directory fails; the game still runs. `kind` goes into
    // the descriptor; empty is "game".
    void start(std::string_view kind = {});
    void stop();

    // Armed clicks run here, before simulate. Queries are not answered yet.
    void begin_frame(const CliFrame &frame);

    // Windows that an armed `screenshot` waits for, each once. GameLoop asks after begin_frame and passes them to
    // draw_all as captures.
    [[nodiscard]] std::vector<WindowId> capture_requests();

    // After draw_all. Answers queries, host commands, clicks that ran in this frame's begin_frame, and screenshots
    // armed by the previous drain from `frame.captures`; arms new screenshots. A UI command whose window has no world
    // is answered with an error at once.
    void drain(const CliFrame &frame);

    [[nodiscard]] std::filesystem::path descriptor_directory();

    // Blocks until a request is queued or the timeout elapses. Tests drain on this thread.
    void wait_for_request();
#else
    inline void start(std::string_view = {}) {}
    inline void stop() {}
    inline void begin_frame(const CliFrame &) {}
    [[nodiscard]] inline std::vector<WindowId> capture_requests() { return {}; }
    inline void drain(const CliFrame &) {}
#endif

} // namespace engine::cli
