#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// Fields of assets/ui/opening.xml. Names are the XML binding paths, so they stay camelCase.
class OpeningViewModel final : public engine::ui::ViewModel {
public:
    OpeningViewModel();

    engine::ui::Bindable<std::string> title;
    engine::ui::Bindable<std::string> stepText;
    engine::ui::Bindable<float> progressWidth{0.0f};
    engine::ui::Bindable<std::string> detailText;
    engine::ui::MethodCommand skip;
};

}
