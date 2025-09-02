#include "text_row_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

TextRowViewModel::TextRowViewModel(std::string name_text, std::string value_text)
    : name(std::move(name_text))
    , value(std::move(value_text)) {
    assets::ui::Inspector::Sections::Groups::TextRows::bind(*this);
}

}
