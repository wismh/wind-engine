#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/command.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

// Fields of assets/ui/world_view.xml. Names are the XML binding paths, so they stay camelCase.
class WorldViewViewModel final : public engine::ui::ViewModel {
public:
    WorldViewViewModel();

    engine::ui::Bindable<std::string> projectionLabel;
    engine::ui::Bindable<std::string> statusText;
    engine::ui::Bindable<std::string> viewportBg;
    engine::ui::Bindable<std::string> emptyText;
    engine::ui::MethodCommand toggleProjection;
    engine::ui::MethodCommand resetCamera;
};

}
