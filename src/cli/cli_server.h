#pragma once

#include <engine/ecs/world.h>

#include <cstdint>
#include <filesystem>
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
        std::string error;
    };

    struct CliResponse {
        std::string json;
        // Profile is waiting for a frame committed while capture is on. The socket thread keeps
        // the job; the next drain calls execute again.
        bool pending = false;
    };

    [[nodiscard]] CliRequest parse_request(std::string_view body);

    // Runs the command on this thread. Click runs immediately; the socket server defers that
    // call to the start of the next frame and answers after that frame's draw.
    [[nodiscard]] CliResponse execute(ecs::World &world, const CliRequest &request);

#if defined(ENGINE_CLI_SERVER)
    // Loopback server. No-op when bind or the descriptor directory fails; the game still runs.
    void start();
    void stop();

    // Armed clicks run here, before simulate. Queries are not answered yet.
    void begin_frame(ecs::World &world);

    // After draw_all. Answers queries and clicks that ran in this frame's begin_frame.
    void drain(ecs::World &world);

    [[nodiscard]] std::filesystem::path descriptor_directory();

    // Blocks until a request is queued or the timeout elapses. Tests drain on this thread.
    void wait_for_request();
#else
    inline void start() {}
    inline void stop() {}
    inline void begin_frame(ecs::World &) {}
    inline void drain(ecs::World &) {}
#endif

} // namespace engine::cli
