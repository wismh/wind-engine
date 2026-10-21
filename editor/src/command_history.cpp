#include "command_history.h"

#include <utility>

namespace editor {

void CommandHistory::set_after_push(std::function<void()> after_push) {
    after_push_ = std::move(after_push);
}

void CommandHistory::push(std::unique_ptr<EditorCommand> command) {
    if (command == nullptr) {
        return;
    }
    if (!undo_.empty() && undo_.back()->merges_with(*command)) {
        undo_.back()->merge_from(*command);
        return;
    }
    undo_.push_back(std::move(command));
    redo_.clear();
    if (after_push_) {
        after_push_();
    }
}

bool CommandHistory::undo() {
    if (undo_.empty()) {
        return false;
    }
    std::unique_ptr<EditorCommand> command = std::move(undo_.back());
    undo_.pop_back();
    if (!command->undo()) {
        undo_.push_back(std::move(command));
        return false;
    }
    redo_.push_back(std::move(command));
    return true;
}

bool CommandHistory::redo() {
    if (redo_.empty()) {
        return false;
    }
    std::unique_ptr<EditorCommand> command = std::move(redo_.back());
    redo_.pop_back();
    if (!command->redo()) {
        redo_.push_back(std::move(command));
        return false;
    }
    undo_.push_back(std::move(command));
    return true;
}

void CommandHistory::clear() {
    undo_.clear();
    redo_.clear();
}

void CommandHistory::discard(EditorCommand::Kind kind) {
    const auto drop = [kind](const std::unique_ptr<EditorCommand>& command) { return command->kind() == kind; };
    std::erase_if(undo_, drop);
    std::erase_if(redo_, drop);
}

int CommandHistory::count(EditorCommand::Kind kind) const {
    int total = 0;
    for (const std::unique_ptr<EditorCommand>& command : undo_) {
        if (command->kind() == kind) {
            ++total;
        }
    }
    return total;
}

}
