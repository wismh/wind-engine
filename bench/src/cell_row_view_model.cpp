#include "cell_row_view_model.h"

#include <engine/ui/binding_id.h>

#include <vector>

namespace bench {

CellRowViewModel::CellRowViewModel(std::size_t count) {
    property(engine::ui::intern("cells"), cells);
    std::vector<std::shared_ptr<CellViewModel>> made;
    made.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        made.push_back(std::make_shared<CellViewModel>());
    }
    cells.set(std::move(made));
}

}
