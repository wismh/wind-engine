#pragma once

#include "table_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace bench {

// Fields of assets/ui/table.xml.
class TableViewModel final : public engine::ui::ViewModel {
public:
    TableViewModel();

    engine::ui::Bindable<std::string> summaryText;
    // The one value the one-change mode writes.
    engine::ui::Bindable<std::string> frameText;
    engine::ui::BindableList<std::shared_ptr<TableRowViewModel>> rows;
};

}
