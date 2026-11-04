#pragma once

// docs/tech/modules/Core.md

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// A dedicated server's command line.
struct ServerOptions {
    // --port. `ServerServices::port` falls back to the config file, then to the game's default.
    std::optional<std::uint16_t> port;
    // --config. Empty: `<executable dir>/server.toml` when there is one. An explicit file has to exist.
    std::filesystem::path config;
    // --assets. Empty: `<executable dir>/assets`. An explicit directory has to exist.
    std::filesystem::path assets;
    // --help: print the usage and exit 0, without starting anything.
    bool help = false;
    // What the engine did not take, in order, for the game. Everything after `--` lands here too.
    std::vector<std::string> args;
};

enum class ServerArgError {
    MissingValue,  // --port, --config, or --assets at the end of the line
    BadPort,       // not a whole number in 1..65535
};

struct ServerArgProblem {
    ServerArgError error = ServerArgError::MissingValue;
    std::string argument;
};

// `arguments` is the command line without the program name, as UTF-8.
[[nodiscard]] std::expected<ServerOptions, ServerArgProblem> parse_server_args(std::span<const std::string> arguments);

// The command line without the program name, as UTF-8. Windows reads it from the wide command line, so a path outside
// the system code page survives; elsewhere it is argv.
[[nodiscard]] std::vector<std::string> process_arguments(int argc, char** argv);

// The engine's flags, for --help.
[[nodiscard]] std::string_view server_usage() noexcept;
void print_server_usage();
// Writes the problem and the usage to stderr. The log is not open yet.
void report_server_arg_problem(const ServerArgProblem& problem);

}
