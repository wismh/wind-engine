#pragma once

#include <filesystem>
#include <string_view>

namespace engine::log {

// No-file / null sink. Does not require SDL. Safe for engine_tests.
void init();

// File sink at <exe_dir>/game.log. Call from Engine::init with the SDL base path.
void init(const std::filesystem::path& exe_dir);

void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}
