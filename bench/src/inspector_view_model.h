#pragma once

#include "section_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace bench {

// Fields of assets/ui/inspector.xml.
class InspectorViewModel final : public engine::ui::ViewModel {
public:
    InspectorViewModel();

    // The one value the one-change mode writes.
    engine::ui::Bindable<std::string> frameText;
    engine::ui::BindableList<std::shared_ptr<SectionViewModel>> sections;
};

}
