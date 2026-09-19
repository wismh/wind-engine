#pragma once

#include "command_history.h"
#include "editor_selection.h"
#include "inspector_field_view_model.h"
#include "inspector_line_view_model.h"
#include "inspector_section.h"
#include "inspector_section_view_model.h"
#include "inspector_view_model.h"
#include "thumbnail_cache.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {
class AssetsDb;
}

namespace engine::ecs {
class World;
}

namespace editor {

// The Inspector tab: the editor's selection (EditorSelection), in sections that collapse.
// - A file or folder of the project: inspect_asset, read from disk when it is selected (again).
// - An entity of the active world (Edit Mode preview or Play Mode game world): inspect_entity, copied on
//   every refresh while the tab is shown.
// - A UI element of the game: the probe's detail of that window's selection (the title and Computed) and the
//   matched Rules, copied on every refresh, so the numbers follow the game.
// Collapsed headings are kept across selections. Holds `this` in its sections, so it never moves.
class InspectorPanel {
public:
    explicit InspectorPanel(const EditorSelection& selection);

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<InspectorViewModel>& view_model() const;

    // The game world a UI element or entity selection is read from, between Play and Stop. Does not attach the
    // probe: the UI Tree panel does.
    void attach(engine::ecs::World& game);
    void detach();
    [[nodiscard]] bool attached() const;

    // The Edit Mode preview world an entity selection is read from when not playing.
    void set_edit_world(engine::ecs::World* edit_world);
    void set_assets(engine::AssetsDb* assets);
    // Makes the preview of a selected texture. Not owned; nullptr shows no preview.
    void set_previews(ThumbnailService* previews);
    // Entity edits are pushed here. `on_history` runs after a step is recorded (the world view syncs its dirty flag).
    void set_journal(CommandHistory* journal, std::function<void()> on_history);

    // While the tab is shown.
    void refresh();

    // True while `refresh` is writing the view-models, so a binding write is not a user edit.
    [[nodiscard]] bool refreshing() const { return refreshing_; }

    // The section with this heading: collapse it, or expand it again.
    void toggle_section(const std::string& heading);
    // Opens or closes the list of components that can be added.
    void toggle_add();
    void add_component(const std::string& name);
    void remove_component(const std::string& name);
    // A text widget changed. `slot` below 0 is the whole value; zero and up is one axis of a vector.
    void edit_text(const std::string& component, const std::string& key, int slot, const std::string& text);
    void edit_bool(const std::string& component, const std::string& key, bool value);
    // The next refresh copies every field from the world again.
    void end_field_edit();

private:
    void show_nothing(std::string title);
    void show_asset(const AssetSelection& asset);
    void show_ui_element(const UiElementSelection& element);
    void show_entity(const engine::ecs::World& world, engine::ecs::Entity entity);
    void show_add(const engine::ecs::World* world, engine::ecs::Entity entity, bool editable);
    // Turns `content` into the section view-models, reusing them by index.
    void show_sections(std::vector<InspectorSection> content);
    [[nodiscard]] bool can_edit() const;
    [[nodiscard]] const EntitySelection* edited_entity() const;
    void note_history();
    // The preview of the selected file when it is a PNG, otherwise none. Each refresh: the image may still be coming.
    void show_preview();
    void hide_preview();

    const EditorSelection* selection_;
    std::shared_ptr<InspectorViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    engine::ecs::World* edit_world_ = nullptr;
    engine::AssetsDb* assets_ = nullptr;
    ThumbnailService* previews_ = nullptr;
    // The selected PNG while its preview is shown or coming.
    std::optional<AssetSelection> preview_asset_;
    ThumbnailStamp preview_stamp_;
    std::uint64_t preview_revision_ = 0;
    CommandHistory* journal_ = nullptr;
    std::function<void()> on_history_;
    bool refreshing_ = false;
    bool add_open_ = false;
    // component, key, and slot of the widget the user is typing into. Empty when they are not.
    std::string editing_key_;
    engine::ecs::Entity editing_entity_{};
    // EditorSelection::revision of the file read last; a file is read again only when it moves.
    std::optional<std::uint64_t> asset_revision_;
    // What the sections show, kept so a toggle shows it again without reading the disk.
    std::vector<InspectorSection> content_;
    std::vector<std::shared_ptr<InspectorSectionViewModel>> sections_;
    // Line view-models of each section, by index, reused across refreshes.
    std::vector<std::vector<std::shared_ptr<InspectorLineViewModel>>> lines_;
    std::vector<std::vector<std::shared_ptr<InspectorFieldViewModel>>> fields_;
    std::vector<std::shared_ptr<InspectorAddViewModel>> addables_;
    std::unordered_set<std::string> collapsed_;
};

}
