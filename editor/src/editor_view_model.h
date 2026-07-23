#pragma once

#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// Fields of assets/ui/editor.xml. Names are the XML binding paths, so they stay camelCase.
class EditorViewModel final : public engine::ui::ViewModel {
public:
    EditorViewModel();

    engine::ui::Bindable<std::string> projectText;
    engine::ui::Bindable<std::string> statusText;
    engine::ui::Bindable<std::string> playLabel{std::string("Play")};
    engine::ui::Bindable<bool> isPlaying;

    MethodCommand togglePlay;
};

}
