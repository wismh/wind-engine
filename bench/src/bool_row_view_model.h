#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// A flag property row of assets/ui/inspector.xml: a name and a Checkbox.
class BoolRowViewModel final : public engine::ui::ViewModel {
public:
    BoolRowViewModel(std::string name_text, bool checked);

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<bool> on;
};

}
