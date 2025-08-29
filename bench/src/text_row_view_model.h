#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// A text property row of assets/ui/inspector.xml: a name and a TextInput.
class TextRowViewModel final : public engine::ui::ViewModel {
public:
    TextRowViewModel(std::string name_text, std::string value_text);

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<std::string> value;
};

}
