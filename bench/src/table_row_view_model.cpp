#include "table_row_view_model.h"

#include <asset_ids.h>

namespace bench {

TableRowViewModel::TableRowViewModel(const TableRecord& record)
    : id(record.id_text)
    , name(record.name)
    , value(record.value)
    , status(record.status)
    , ratio(record.ratio)
    , stripe(std::string(record.id % 2 == 0 ? "#222733" : "#1b1f27")) {
    assets::ui::Table::Rows::bind(*this);
    select.bind_to<TableRowViewModel, &TableRowViewModel::pick>(*this);
}

void TableRowViewModel::pick() {
    picked_ = true;
}

}
