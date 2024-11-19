#include <engine/log.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>
#include <vector>

namespace engine::log {
namespace {

std::shared_ptr<spdlog::logger> make_logger(std::vector<spdlog::sink_ptr> sinks) {
    auto logger = std::make_shared<spdlog::logger>("engine", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    return logger;
}

std::shared_ptr<spdlog::logger> make_logger(spdlog::sink_ptr sink) {
    return make_logger(std::vector<spdlog::sink_ptr>{std::move(sink)});
}

std::shared_ptr<spdlog::logger>& current() {
    static std::shared_ptr<spdlog::logger> logger =
            make_logger(std::make_shared<spdlog::sinks::null_sink_mt>());
    return logger;
}

spdlog::logger& logger() {
    return *current();
}

}

void init() {
    current() = make_logger(std::make_shared<spdlog::sinks::null_sink_mt>());
}

void init(const std::filesystem::path& exe_dir) {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)exe_dir;
    current() = make_logger(std::make_shared<spdlog::sinks::stdout_sink_st>());
    return;
#endif
    if (exe_dir.empty()) {
        init();
        return;
    }
    try {
        const std::filesystem::path path = exe_dir / "game.log";
        std::vector<spdlog::sink_ptr> sinks{std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true)};
#ifndef NDEBUG
        // Debug game executables keep a console window (engine_add_game hides it in Release-ish
        // configs), so mirror log output there too instead of leaving it file-only.
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif
        current() = make_logger(std::move(sinks));
    } catch (...) {
        init();
    }
}

void info(std::string_view message) {
    logger().info("{}", message);
}

void warn(std::string_view message) {
    logger().warn("{}", message);
}

void error(std::string_view message) {
    logger().error("{}", message);
}

}
