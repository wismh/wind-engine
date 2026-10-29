#pragma once

#include "inspector_field_view_model.h"
#include "inspector_section_view_model.h"

#include <engine/resources/asset_id.h>
#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/inspector.xml. Names are the XML binding paths, so they stay camelCase.
class InspectorViewModel final : public engine::ui::ViewModel {
public:
    InspectorViewModel();

    // What is selected: a file name, or an element's `Kind #id .class`, or why nothing is shown.
    engine::ui::Bindable<std::string> title;
    // What kind of thing it is, or how to select something.
    engine::ui::Bindable<std::string> subtitle;
    engine::ui::BindableList<std::shared_ptr<InspectorSectionViewModel>> sections;
    // The selected texture, drawn under the sections. `previewDisplay` is "flex" while it is ready; the size is
    // the image's own, in CSS pixels ("200px"), so the box keeps its aspect.
    engine::ui::Bindable<engine::AssetId> preview;
    engine::ui::Bindable<std::string> previewDisplay{std::string("none")};
    engine::ui::Bindable<std::string> previewWidth{std::string("0px")};
    engine::ui::Bindable<std::string> previewHeight{std::string("0px")};
    // "flex" while an entity is open in Edit Mode.
    engine::ui::Bindable<std::string> addDisplay{std::string("none")};
    engine::ui::MethodCommand toggleAdd;
    engine::ui::BindableList<std::shared_ptr<InspectorAddViewModel>> addables;
};

}
