#pragma once

#include "build_line_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace editor {

// Fields of assets/ui/build.xml. Names are the XML binding paths, so they stay camelCase.
class BuildViewModel final : public engine::ui::ViewModel {
public:
    BuildViewModel();

    engine::ui::Bindable<std::string> summary;
    engine::ui::BindableList<std::shared_ptr<BuildLineViewModel>> lines;
    // Two-way scroll offset of the log. The panel sets it past the end when lines arrive; layout clamps it.
    engine::ui::Bindable<float> logScroll;
};

}
