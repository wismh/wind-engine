#pragma once

#include "cell_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <memory>

namespace bench {

// One row of a cell grid (`cells`), shared by the paint-mix documents and the motion scene.
class CellRowViewModel final : public engine::ui::ViewModel {
public:
    explicit CellRowViewModel(std::size_t count);

    engine::ui::BindableList<std::shared_ptr<CellViewModel>> cells;
};

}
