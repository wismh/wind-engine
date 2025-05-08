#pragma once

// docs/tech/features/CLI.md

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace engine {

// A `wind-cli` command the engine does not answer itself (anything but `tree`, `element`, `hit`, `click`,
// `profile`). `path` is the request's optional `path` field.
struct CliCommand {
    std::string name;
    std::string path;
};

// One field of a reply. The server writes it as JSON null, a boolean, an integer, a number, or a string.
using CliValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

// `ok` writes `{"ok":true,"result":{fields}}`; otherwise `{"ok":false,"error":error}`.
struct CliReply {
    bool ok = true;
    std::string error;
    std::vector<std::pair<std::string, CliValue>> result;
};

// The host's own `wind-cli` commands, called on the main thread after the frame drew and before
// `RunHooks::on_frame_end`. `handle` returns nullopt for a command it does not know. `kind` goes into the
// descriptor so `wind-cli` can tell an editor from a game; empty is "game". Without `ENGINE_CLI_SERVER`
// nothing listens and neither is used.
struct CliCommands {
    std::string kind;
    std::function<std::optional<CliReply>(const CliCommand&)> handle;
};

}
