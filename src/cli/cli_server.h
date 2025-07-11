#pragma once

// docs/tech/features/CLI.md

#include <engine/core/cli_commands.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/world.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

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
        // `open`: the project directory. Passed to the host as `CliCommand::path`.
        std::string path;
        std::string error;
    };

    struct CliResponse {
        std::string json;
        // Profile is waiting for a frame committed while capture is on. The socket thread keeps
        // the job; the next drain calls execute again.
        bool pending = false;
    };

    // What a frame offers the server: the world bound to each window, and the host's own commands.
    struct CliFrame {
        std::function<ecs::World *(WindowId)> world_for;
        const CliCommands *host = nullptr;
    };

    [[nodiscard]] CliRequest parse_request(std::string_view body);

    // `{"ok":false,"error":message}`.
    [[nodiscard]] std::string error_json(std::string_view message);

    // `tree`, `element`, `hit`, `click`, and `profile`: answered from the world bound to the request's window.
    [[nodiscard]] bool is_ui_command(std::string_view command);

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

    // After draw_all. Answers queries, host commands, and clicks that ran in this frame's begin_frame. A UI
    // command whose window has no world is answered with an error at once.
    void drain(const CliFrame &frame);

    [[nodiscard]] std::filesystem::path descriptor_directory();

    // Blocks until a request is queued or the timeout elapses. Tests drain on this thread.
    void wait_for_request();
#else
    inline void start(std::string_view = {}) {}
    inline void stop() {}
    inline void begin_frame(const CliFrame &) {}
    inline void drain(const CliFrame &) {}
#endif

} // namespace engine::cli
