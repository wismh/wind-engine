#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// A numeric property row of assets/ui/inspector.xml: a name, a stepper on each side of a TextInput.
class NumberRowViewModel final : public engine::ui::ViewModel {
public:
    NumberRowViewModel(std::string name_text, int number);

    void step_down();
    void step_up();

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<std::string> value;
    MethodCommand decrease;
    MethodCommand increase;

private:
    int number_ = 0;
};

}
