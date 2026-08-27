#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// One row of the HUD's unit list. `select` makes the row hit-testable.
class UnitRowViewModel final : public engine::ui::ViewModel {
public:
    UnitRowViewModel(std::string name_text, std::string hp_text, std::string task_text);

    void pick();

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<std::string> hp;
    engine::ui::Bindable<std::string> task;
    MethodCommand select;

private:
    bool picked_ = false;
};

}
