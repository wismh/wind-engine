#pragma once

#include "cell_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// Fields of the four paint-mix documents (`rows` of `cells`). Registered by hand because they share it.
class PaintMixViewModel final : public engine::ui::ViewModel {
public:
    PaintMixViewModel();

    engine::ui::BindableList<std::shared_ptr<CellRowViewModel>> rows;
};

}
