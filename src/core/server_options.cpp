#include <engine/core/server_options.h>

#include <charconv>
#include <cstdio>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>
#endif

namespace engine {

namespace {

[[nodiscard]] std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

[[nodiscard]] std::optional<std::uint16_t> parse_port(std::string_view text) {
    int value = 0;
    const char* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || ptr != end || value < 1 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

// `--name value` or `--name=value`. Sets `value` and moves `index` past what it used.
[[nodiscard]] bool flag_value(std::span<const std::string> arguments, std::size_t& index, std::string_view name,
        std::optional<std::string_view>& value) {
    const std::string_view argument = arguments[index];
    if (argument == name) {
        if (index + 1 < arguments.size()) {
            ++index;
            value = arguments[index];
        }
        return true;
    }
    if (argument.size() > name.size() && argument.starts_with(name) && argument[name.size()] == '=') {
        value = argument.substr(name.size() + 1);
        return true;
    }
    return false;
}

}

std::expected<ServerOptions, ServerArgProblem> parse_server_args(std::span<const std::string> arguments) {
    ServerOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "--") {
            options.args.insert(options.args.end(), arguments.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                    arguments.end());
            break;
        }
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }
        std::optional<std::string_view> value;
        if (flag_value(arguments, index, "--port", value)) {
            if (!value) {
                return std::unexpected(ServerArgProblem{ServerArgError::MissingValue, argument});
            }
            options.port = parse_port(*value);
            if (!options.port) {
                return std::unexpected(ServerArgProblem{ServerArgError::BadPort, std::string(*value)});
            }
        } else if (flag_value(arguments, index, "--config", value)) {
            if (!value || value->empty()) {
                return std::unexpected(ServerArgProblem{ServerArgError::MissingValue, argument});
            }
            options.config = utf8_path(*value);
        } else if (flag_value(arguments, index, "--assets", value)) {
            if (!value || value->empty()) {
                return std::unexpected(ServerArgProblem{ServerArgError::MissingValue, argument});
            }
            options.assets = utf8_path(*value);
        } else {
            options.args.push_back(argument);
        }
    }
    return options;
}

std::vector<std::string> process_arguments(int argc, char** argv) {
    std::vector<std::string> arguments;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    int count = 0;
    wchar_t** const wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr) {
        return arguments;
    }
    for (int i = 1; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            arguments.emplace_back();
            continue;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
        text.pop_back();  // the terminator the -1 length counted
        arguments.push_back(std::move(text));
    }
    LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
#endif
    return arguments;
}

std::string_view server_usage() noexcept {
    return "Options:\n"
           "  --port <1..65535>  the port to listen on (else `port` in the config file, else the game's default)\n"
           "  --config <file>    a TOML config file (default: server.toml beside the executable, when there is one)\n"
           "  --assets <dir>     the cooked assets (default: assets beside the executable)\n"
           "  --help             this text\n"
           "Any other argument, and everything after --, goes to the game.\n";
}

void print_server_usage() {
    const std::string_view usage = server_usage();
    std::fwrite(usage.data(), 1, usage.size(), stdout);
    std::fflush(stdout);
}

void report_server_arg_problem(const ServerArgProblem& problem) {
    const char* const what = problem.error == ServerArgError::BadPort ? "is not a port (1..65535)" : "needs a value";
    std::fprintf(stderr, "%s %s\n", problem.argument.c_str(), what);
    const std::string_view usage = server_usage();
    std::fwrite(usage.data(), 1, usage.size(), stderr);
    std::fflush(stderr);
}

}
