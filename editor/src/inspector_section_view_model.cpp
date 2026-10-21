#include "inspector_section_view_model.h"

#include "inspector_panel.h"

#include <asset_ids.h>

namespace editor {

InspectorSectionViewModel::InspectorSectionViewModel(InspectorPanel& panel) : panel_(&panel) {
    assets::ui::Inspector::Sections::bind(*this);
    toggle.bind_to<InspectorSectionViewModel, &InspectorSectionViewModel::toggle_section>(*this);
    remove.bind_to<InspectorSectionViewModel, &InspectorSectionViewModel::remove_component>(*this);
}

void InspectorSectionViewModel::remove_component() {
    panel_->remove_component(heading.get());
}

void InspectorSectionViewModel::toggle_section() {
    panel_->toggle_section(heading.get());
}

}
