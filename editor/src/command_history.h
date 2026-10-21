#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace editor {

// One editor change that has already been applied. `undo` reverses it and `redo` applies it again. A false return
// leaves the stacks as they were: the step could not run (a file that changed on disk, an entity that is gone).
class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    enum class Kind { Project, Entity };

    virtual bool undo() = 0;
    virtual bool redo() = 0;
    [[nodiscard]] virtual Kind kind() const = 0;

    // A later edit of the same field replaces this step's result instead of pushing another step, so one undo
    // restores the value from before the first keystroke.
    [[nodiscard]] virtual bool merges_with(const EditorCommand&) const { return false; }
    virtual void merge_from(EditorCommand&) {}
};

// The editor's one undo stack. File steps and entity steps share it, so Undo reverses whichever happened last.
// `push` drops the redo stack. `discard` pulls every step of one kind out of both stacks (a world reload drops
// entity steps and leaves the file steps).
class CommandHistory {
public:
    void push(std::unique_ptr<EditorCommand> command);
    // Runs after a step is pushed and is not a merge. A file step uses it to drop the project redo stack, which
    // lives beside this one.
    void set_after_push(std::function<void()> after_push);

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    void clear();
    void discard(EditorCommand::Kind kind);

    [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_.empty(); }
    [[nodiscard]] int count(EditorCommand::Kind kind) const;

private:
    std::vector<std::unique_ptr<EditorCommand>> undo_;
    std::vector<std::unique_ptr<EditorCommand>> redo_;
    std::function<void()> after_push_;
};

}
