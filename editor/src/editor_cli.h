#pragma once

#include "toolbar.h"

#include <engine/core/cli_commands.h>

#include <filesystem>
#include <optional>
#include <string>

namespace editor {

// What `state` reports beyond the toolbar, read from EditorApp when a command arrives.
struct EditorFacts {
    // The project that was read and fits the SDK; empty when none.
    std::string project;
    // The directory last opened, readable or not; empty when none.
    std::filesystem::path project_dir;
    // This editor's SDK version; empty when it does not run from an installed SDK.
    std::string sdk;
    bool dialog_open = false;
};

// The editor's `wind-cli` commands: `state`, `play`, `stop`, and `open <dir>`. Like the toolbar buttons they only
// record a request; EditorApp acts on it in on_frame_end, so `play` answers before the build starts and `state`
// shows what happened. Holds the toolbar by reference, so it never outlives it.
class EditorCli {
public:
    explicit EditorCli(Toolbar& toolbar);

    // Nullopt for a command that is not the editor's.
    [[nodiscard]] std::optional<engine::CliReply> handle(const engine::CliCommand& command, const EditorFacts& facts);

    // The directory `open` asked for since the previous call.
    [[nodiscard]] std::optional<std::filesystem::path> take_open();

private:
    [[nodiscard]] engine::CliReply state(const EditorFacts& facts) const;
    [[nodiscard]] engine::CliReply play();
    [[nodiscard]] engine::CliReply stop();
    [[nodiscard]] engine::CliReply open(const std::string& path, const EditorFacts& facts);

    Toolbar* toolbar_;
    std::optional<std::filesystem::path> open_;
};

}
