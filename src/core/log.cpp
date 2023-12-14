#include <engine/log.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace engine::log {
namespace {

std::shared_ptr<spdlog::logger> make_logger(spdlog::sink_ptr sink) {
    auto logger = std::make_shared<spdlog::logger>("engine", std::move(sink));
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    return logger;
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
    if (exe_dir.empty()) {
        init();
        return;
    }
    try {
        const std::filesystem::path path = exe_dir / "game.log";
        current() = make_logger(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
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
