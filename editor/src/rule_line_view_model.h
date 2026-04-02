#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// One matched CSS rule in the inspector (`rules` in assets/ui/inspector.xml).
class RuleLineViewModel final : public engine::ui::ViewModel {
public:
    RuleLineViewModel();

    engine::ui::Bindable<std::string> line;
};

}
