#pragma once

#include "editor_selection.h"
#include "inspector_line_view_model.h"
#include "inspector_section.h"
#include "inspector_section_view_model.h"
#include "inspector_view_model.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::ecs {
class World;
}

namespace editor {

// The Inspector tab: the editor's selection (EditorSelection), in sections that collapse.
// - A file or folder of the project: inspect_asset, read from disk when it is selected (again).
// - A UI element of the game: the probe's detail of that window's selection (the title and Computed) and the
//   matched Rules, copied on every refresh, so the numbers follow the game.
// Collapsed headings are kept across selections. Holds `this` in its sections, so it never moves.
class InspectorPanel {
public:
    explicit InspectorPanel(const EditorSelection& selection);

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<InspectorViewModel>& view_model() const;

    // The game world a UI element selection is read from, between Play and Stop. Does not attach the probe:
    // the UI Tree panel does.
    void attach(engine::ecs::World& game);
    void detach();
    [[nodiscard]] bool attached() const;

    // While the tab is shown.
    void refresh();

    // The section with this heading: collapse it, or expand it again.
    void toggle_section(const std::string& heading);

private:
    void show_nothing(std::string title);
    void show_asset(const AssetSelection& asset);
    void show_ui_element(const UiElementSelection& element);
    // Turns `content` into the section view-models, reusing them by index.
    void show_sections(std::vector<InspectorSection> content);

    const EditorSelection* selection_;
    std::shared_ptr<InspectorViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    // EditorSelection::revision of the file read last; a file is read again only when it moves.
    std::optional<std::uint64_t> asset_revision_;
    // What the sections show, kept so a toggle shows it again without reading the disk.
    std::vector<InspectorSection> content_;
    std::vector<std::shared_ptr<InspectorSectionViewModel>> sections_;
    // Line view-models of each section, by index, reused across refreshes.
    std::vector<std::vector<std::shared_ptr<InspectorLineViewModel>>> lines_;
    std::unordered_set<std::string> collapsed_;
};

}
