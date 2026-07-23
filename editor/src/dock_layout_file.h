#pragma once

#include <engine/ui/dock_layout.h>

#include <filesystem>
#include <optional>

namespace editor {

// The file the editor keeps its panel layout in (dock_layout_to_text). An empty path reads nothing and writes nothing:
// the editor then starts from the default layout every time.
class DockLayoutFile {
public:
    DockLayoutFile() = default;
    explicit DockLayoutFile(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const;

    // The saved layout. Nullopt when there is no file, it does not read, or it is not a layout (logged).
    [[nodiscard]] std::optional<engine::ui::DockLayout> load() const;
    // Writes the layout next to the file and renames it over the file, so a crash mid-write keeps the old one. Creates
    // the directory. False (logged) when it fails.
    bool save(const engine::ui::DockLayout& layout) const;

private:
    std::filesystem::path path_;
};

}
