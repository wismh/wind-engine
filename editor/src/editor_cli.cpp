#include "editor_cli.h"

#include <engine/project/wind_project.h>

#include <utility>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

const char* run_text(RunState state) {
    switch (state) {
        case RunState::Idle:
            return "idle";
        case RunState::Building:
            return "building";
        case RunState::Playing:
            return "playing";
    }
    return "idle";
}

engine::CliReply failure(std::string error) {
    return engine::CliReply{.ok = false, .error = std::move(error), .result = {}};
}

engine::CliValue text_or_null(std::string text) {
    if (text.empty()) {
        return std::monostate{};
    }
    return std::move(text);
}

}

EditorCli::EditorCli(Toolbar& toolbar) : toolbar_(&toolbar) {}

std::optional<engine::CliReply> EditorCli::handle(const engine::CliCommand& command, const EditorFacts& facts) {
    if (command.name == "state") {
        return state(facts);
    }
    if (command.name == "play") {
        return play();
    }
    if (command.name == "stop") {
        return stop();
    }
    if (command.name == "open") {
        return open(command.path);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> EditorCli::take_open() {
    return std::exchange(open_, std::nullopt);
}

engine::CliReply EditorCli::state(const EditorFacts& facts) const {
    engine::CliReply reply;
    reply.result = {
            {"run", std::string(run_text(toolbar_->state()))},
            {"playable", toolbar_->playable()},
            {"status", toolbar_->status()},
            {"project", text_or_null(facts.project)},
            {"project_dir", text_or_null(path_text(facts.project_dir))},
            {"sdk", text_or_null(facts.sdk)},
    };
    return reply;
}

engine::CliReply EditorCli::play() {
    if (toolbar_->state() != RunState::Idle) {
        return failure(std::string("already ") + run_text(toolbar_->state()));
    }
    if (!toolbar_->playable()) {
        return failure("not playable: " + toolbar_->status());
    }
    toolbar_->toggle_play();
    engine::CliReply reply;
    reply.result = {{"requested", std::string("play")}};
    return reply;
}

engine::CliReply EditorCli::stop() {
    const RunState state = toolbar_->state();
    if (state == RunState::Idle) {
        return failure("not playing");
    }
    toolbar_->toggle_play();
    engine::CliReply reply;
    reply.result = {{"requested", std::string("stop")}, {"was", std::string(run_text(state))}};
    return reply;
}

engine::CliReply EditorCli::open(const std::string& path) {
    if (path.empty()) {
        return failure("open needs a path");
    }
    std::filesystem::path directory(std::u8string(path.begin(), path.end()));
    if (directory.filename() == engine::kWindProjectFile) {
        directory = directory.parent_path();
    }
    if (!directory.is_absolute()) {
        return failure("open needs an absolute path");
    }
    if (toolbar_->state() != RunState::Idle) {
        return failure(std::string("stop first: the editor is ") + run_text(toolbar_->state()));
    }
    open_ = directory;
    engine::CliReply reply;
    reply.result = {{"requested", std::string("open")}, {"path", path_text(directory)}};
    return reply;
}

}
