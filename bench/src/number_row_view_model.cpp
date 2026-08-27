#include "number_row_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

NumberRowViewModel::NumberRowViewModel(std::string name_text, int number)
    : name(std::move(name_text))
    , value(std::to_string(number))
    , number_(number) {
    assets::ui::Inspector::Sections::Groups::NumberRows::bind(*this);
    decrease.bind_to<NumberRowViewModel, &NumberRowViewModel::step_down>(*this);
    increase.bind_to<NumberRowViewModel, &NumberRowViewModel::step_up>(*this);
}

void NumberRowViewModel::step_down() {
    --number_;
    value = std::to_string(number_);
}

void NumberRowViewModel::step_up() {
    ++number_;
    value = std::to_string(number_);
}

}
