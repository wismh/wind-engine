#pragma once

#include "inspector_field_view_model.h"
#include "inspector_line_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

class InspectorPanel;

// One section of the Inspector tab (`sections` in assets/ui/inspector.xml): a heading that collapses and
// expands it, and its lines. A collapsed section shows no lines.
class InspectorSectionViewModel final : public engine::ui::ViewModel {
public:
    explicit InspectorSectionViewModel(InspectorPanel& panel);

    InspectorSectionViewModel(const InspectorSectionViewModel&) = delete;
    InspectorSectionViewModel& operator=(const InspectorSectionViewModel&) = delete;

    // The chevron and the heading button.
    void toggle_section();
    void remove_component();

    engine::ui::Bindable<std::string> heading;
    // The chevron's `checked`: turned down while expanded.
    engine::ui::Bindable<bool> expanded;
    engine::ui::BindableList<std::shared_ptr<InspectorLineViewModel>> lines;
    engine::ui::BindableList<std::shared_ptr<InspectorFieldViewModel>> fields;
    engine::ui::Bindable<std::string> removeDisplay{std::string("none")};
    engine::ui::MethodCommand toggle;
    engine::ui::MethodCommand remove;

private:
    InspectorPanel* panel_;
};

}
