#pragma once

#include "method_command.h"
#include "table_record.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// One row of assets/ui/table.xml. `select` makes the row hit-testable, so the pointer can hover it.
class TableRowViewModel final : public engine::ui::ViewModel {
public:
    explicit TableRowViewModel(const TableRecord& record);

    void pick();
    [[nodiscard]] bool picked() const { return picked_; }

    engine::ui::Bindable<std::string> id;
    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<std::string> value;
    engine::ui::Bindable<std::string> status;
    engine::ui::Bindable<std::string> ratio;
    // The zebra stripe through `var-stripe`, by the record's id so a row keeps its color when rows move.
    engine::ui::Bindable<std::string> stripe;
    MethodCommand select;

private:
    bool picked_ = false;
};

}
