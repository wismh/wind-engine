#pragma once

#include "command_history.h"

#include <engine/ecs/world_io.h>

#include <filesystem>
#include <string>

namespace engine {
class AssetsDb;
}

namespace editor {

// Reads the object file and populates `out_type` and `out_table` (merging schema defaults when available).
[[nodiscard]] bool read_object_table(const std::filesystem::path& path, const engine::AssetsDb* assets,
        std::string& out_type, engine::ecs::FieldTable& out_table);

// Serializes `table` with `type` and writes it to `path`.
[[nodiscard]] bool write_object_table(const std::filesystem::path& path, const std::string& type,
        const engine::ecs::FieldTable& table);

// Modifies one text field of an object file and pushes an undo step to journal when present.
[[nodiscard]] bool edit_object_field_text(const std::filesystem::path& path, std::string_view project_key,
        const std::string& type, engine::AssetsDb* assets, CommandHistory* journal,
        const std::string& key, int slot, const std::string& text);

// Modifies one bool field of an object file and pushes an undo step to journal when present.
[[nodiscard]] bool edit_object_field_bool(const std::filesystem::path& path,
        const std::string& type, engine::AssetsDb* assets, CommandHistory* journal,
        const std::string& key, bool value);

// An undoable inspector edit of a .object file on disk.
class ObjectEditCommand final : public EditorCommand {
public:
    ObjectEditCommand(std::filesystem::path path, std::string type, engine::AssetsDb* assets,
            engine::ecs::FieldTable before, engine::ecs::FieldTable after, std::string coalesce);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Project; }
    [[nodiscard]] bool valid() const override;
    [[nodiscard]] bool merges_with(const EditorCommand& other) const override;
    void merge_from(EditorCommand& newer) override;

private:
    [[nodiscard]] bool apply(const engine::ecs::FieldTable& table);

    std::filesystem::path path_;
    std::string type_;
    engine::AssetsDb* assets_ = nullptr;
    engine::ecs::FieldTable before_;
    engine::ecs::FieldTable after_;
    std::string coalesce_;
};

} // namespace editor
