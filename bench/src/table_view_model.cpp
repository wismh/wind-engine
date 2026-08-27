#include "table_view_model.h"

#include <asset_ids.h>

namespace bench {

TableViewModel::TableViewModel() {
    assets::ui::Table::bind(*this);
}

}
