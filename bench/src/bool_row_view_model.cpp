#include "bool_row_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

BoolRowViewModel::BoolRowViewModel(std::string name_text, bool checked)
    : name(std::move(name_text))
    , on(checked) {
    assets::ui::Inspector::Sections::Groups::BoolRows::bind(*this);
}

}
