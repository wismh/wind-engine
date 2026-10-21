#pragma once

#include "command_history.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor {

// One path a project edit moved, relative to the project directory (`assets/a` -> `assets/b`). Undo of that edit
// reports the pair reversed, so a listener can follow an open file either way.
struct ProjectRelocation {
    std::string from;
    std::string to;
};

// What one project edit did. `ok` false changed nothing on disk and pushed nothing onto the history. `notice` is a
// line for the editor status (a refusal, or the note that a new .hpp/.cpp is not in the build yet).
struct ProjectEditResult {
    bool ok = false;
    std::string notice;
    // The tree key to select after this step. Empty leaves the selection to whatever still exists.
    std::string select;
    std::vector<ProjectRelocation> moved;
    // Keys `remove` deleted. Undo of that step leaves this empty: the file is back.
    std::vector<std::string> removed;
};

enum class ProjectCreateKind {
    Directory,
    World,
    Header,
    Source,
};

// Told after a step ran (undo or redo), or failed to. A failure has `ok` false and a notice. The editor's explorer
// implements it to refresh its tree and selection.
class ProjectStepListener {
public:
    virtual ~ProjectStepListener() = default;
    virtual void project_step_ran(const ProjectEditResult& result) = 0;
};

class ProjectStep;

// What starting one project edit did. `step` is set exactly when `result.ok` is true: push it on the editor's
// `CommandHistory`, which owns it from then on.
struct ProjectEditOutcome {
    ProjectEditResult result;
    std::unique_ptr<ProjectStep> step;
};

// One undoable create, copy, relocate, or delete under a project directory. The factories do the edit on disk and
// hand back the step that can reverse it; `undo` and `redo` on the step then walk it back and forth.
//
// A relocate keeps the asset GUID by moving the sibling `.meta` with the file. A copy and a create under `assets/`
// write a new sidecar through `write_missing_metas` (never a copy of the old GUID). A delete keeps the file bytes
// and the sidecar so undo writes both back.
class ProjectStep final : public EditorCommand {
public:
    [[nodiscard]] static ProjectEditOutcome relocate(ProjectStepListener& listener, const std::filesystem::path& root,
            std::string from, std::string to);
    [[nodiscard]] static ProjectEditOutcome copy_entry(ProjectStepListener& listener,
            const std::filesystem::path& root, std::string from, std::string to);
    [[nodiscard]] static ProjectEditOutcome create(ProjectStepListener& listener, const std::filesystem::path& root,
            std::string key, ProjectCreateKind kind);
    [[nodiscard]] static ProjectEditOutcome remove(ProjectStepListener& listener, const std::filesystem::path& root,
            std::string key);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Project; }
    // False when this step's files or sidecar GUIDs are not what the step left on disk.
    [[nodiscard]] bool valid() const override;

private:
    struct MetaExpect {
        bool sibling = false;
        std::string relative;
        std::string guid;
        // The sidecar bytes, so a delete can write the same file back. Empty when the step only checks the GUID.
        std::string text;
    };

    // One file or folder that was inside a deleted directory, relative to that directory. Empty `relative` is the
    // directory itself.
    struct StoredFile {
        std::string relative;
        bool directory = false;
        std::string bytes;
    };

    struct Edit {
        enum class Kind { Relocate, Copy, CreateFile, CreateDir, Delete } kind = Kind::Relocate;
        std::string from;
        std::string to;
        std::string bytes;
        std::vector<StoredFile> files;
        std::vector<MetaExpect> metas;
        bool applied = false;
    };

    ProjectStep(ProjectStepListener& listener, std::filesystem::path root, Edit edit);

    [[nodiscard]] static ProjectEditOutcome refuse(std::string notice);
    [[nodiscard]] static ProjectEditOutcome finish(ProjectStepListener& listener, const std::filesystem::path& root,
            Edit edit, std::string notice);
    [[nodiscard]] static ProjectEditResult result_of(const Edit& edit, bool forward);
    [[nodiscard]] static bool apply(Edit& edit, const std::filesystem::path& root, std::string& notice);
    [[nodiscard]] static bool revert(Edit& edit, const std::filesystem::path& root, std::string& notice);
    [[nodiscard]] static bool matches_one(const Edit& edit, const std::filesystem::path& root);

    // `suffix` is appended to the asset path (".meta") or is a relative path under a copied/moved directory.
    [[nodiscard]] static std::filesystem::path meta_file(const std::filesystem::path& asset, const MetaExpect& expect);
    static void collect_metas(const std::filesystem::path& asset, bool directory, std::vector<MetaExpect>& out);
    [[nodiscard]] static bool metas_match(const std::filesystem::path& asset, const std::vector<MetaExpect>& metas);
    [[nodiscard]] static bool capture_tree(const std::filesystem::path& path, const std::string& relative,
            std::vector<StoredFile>& out, std::string& notice);
    [[nodiscard]] static bool restore_deleted(const std::filesystem::path& asset, const Edit& edit, std::string& notice);

    ProjectStepListener* listener_;
    std::filesystem::path root_;
    Edit edit_;
};

// A single path segment: no separators, no empty name, no trailing dot or space.
[[nodiscard]] bool project_name_ok(std::string_view name);

[[nodiscard]] std::string project_parent_key(std::string_view key);
[[nodiscard]] std::string project_leaf_name(std::string_view key);
[[nodiscard]] std::string project_join_key(std::string_view parent, std::string_view name);

// `stem` + `extension` in `directory`, then `stem` + " 2" + extension, and so on. Empty when every try exists.
[[nodiscard]] std::string project_unique_name(const std::filesystem::path& directory, std::string_view stem,
        std::string_view extension);

}
