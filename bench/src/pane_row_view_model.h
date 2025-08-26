#pragma once

#include "pane_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// One row of inner ScrollViews in the clip scene.
class PaneRowViewModel final : public engine::ui::ViewModel {
public:
    PaneRowViewModel();

    engine::ui::BindableList<std::shared_ptr<PaneViewModel>> panes;
};

}
