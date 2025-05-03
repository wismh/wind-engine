#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// One line of the build log (`lines` in assets/ui/build.xml).
class BuildLineViewModel final : public engine::ui::ViewModel {
public:
    BuildLineViewModel();

    engine::ui::Bindable<std::string> text;
    // The line's color through `var-tone`. Empty: the stylesheet's default.
    engine::ui::Bindable<std::string> tone;
};

}
