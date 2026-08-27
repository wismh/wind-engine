#pragma once

#include "cell_row_view_model.h"
#include "cell_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// Fields of assets/ui/motion.xml: `rows` of boxes and the `bars`.
class MotionViewModel final : public engine::ui::ViewModel {
public:
    MotionViewModel();

    engine::ui::BindableList<std::shared_ptr<CellRowViewModel>> rows;
    engine::ui::BindableList<std::shared_ptr<CellViewModel>> bars;
};

}
