#pragma once

#include <filesystem>
#include <functional>
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

// Undoable creates, copies, and relocates under one project directory. Each successful `create` / `copy_entry` /
// `relocate` is one step; `undo` and `redo` walk that stack. Nothing here reads the keyboard: Ctrl+Z will call
// `undo` later, and every step already knows how to reverse itself.
//
// A relocate keeps the asset GUID by moving the sibling `.meta` with the file. A copy and a create under `assets/`
// write a new sidecar through `write_missing_metas` (never a copy of the old GUID).
class ProjectHistory {
public:
    [[nodiscard]] ProjectEditResult relocate(const std::filesystem::path& root, std::string from, std::string to);
    [[nodiscard]] ProjectEditResult copy_entry(const std::filesystem::path& root, std::string from, std::string to);
    [[nodiscard]] ProjectEditResult create(const std::filesystem::path& root, std::string key, ProjectCreateKind kind);
    // Deletes `key` and its sibling `.meta`. Undo writes both back, so the GUID stays.

    [[nodiscard]] ProjectEditResult remove(const std::filesystem::path& root, std::string key);

    [[nodiscard]] ProjectEditResult undo(const std::filesystem::path& root);
    [[nodiscard]] ProjectEditResult redo(const std::filesystem::path& root);

    void clear();
    // Drops steps that were undone, without touching the undo stack. The shared command history calls this when a
    // new step (a file or an entity) forgets the redo side.
    void clear_redo();

    // `recorded` runs after a new step is pushed. `cleared` runs after `clear`. Undo and redo do not call them.
    void set_hooks(std::function<void()> recorded, std::function<void()> cleared);

    // False when a step's files or sidecar GUIDs are not what that step left on disk. The caller drops the stack.
    [[nodiscard]] bool matches(const std::filesystem::path& root) const;

    [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_.empty(); }

private:
    // `suffix` is appended to the asset path (".meta") or is a relative path under a copied/moved directory.
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

    [[nodiscard]] ProjectEditResult finish(Edit edit, bool ok, std::string notice);
    [[nodiscard]] bool apply(Edit& edit, const std::filesystem::path& root, std::string& notice);
    [[nodiscard]] bool revert(Edit& edit, const std::filesystem::path& root, std::string& notice);
    [[nodiscard]] bool matches_one(const Edit& edit, const std::filesystem::path& root) const;

    [[nodiscard]] static std::filesystem::path meta_file(const std::filesystem::path& asset, const MetaExpect& expect);
    static void collect_metas(const std::filesystem::path& asset, bool directory, std::vector<MetaExpect>& out);
    [[nodiscard]] static bool metas_match(const std::filesystem::path& asset, const std::vector<MetaExpect>& metas);
    [[nodiscard]] static bool capture_tree(const std::filesystem::path& path, const std::string& relative,
            std::vector<StoredFile>& out, std::string& notice);
    [[nodiscard]] static bool restore_deleted(const std::filesystem::path& asset, const Edit& edit, std::string& notice);

    std::vector<Edit> undo_;
    std::vector<Edit> redo_;
    std::function<void()> recorded_;
    std::function<void()> cleared_;
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
