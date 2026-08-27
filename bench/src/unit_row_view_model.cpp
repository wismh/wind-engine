#include "unit_row_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

UnitRowViewModel::UnitRowViewModel(std::string name_text, std::string hp_text, std::string task_text)
    : name(std::move(name_text))
    , hp(std::move(hp_text))
    , task(std::move(task_text)) {
    assets::ui::Hud::Units::bind(*this);
    select.bind_to<UnitRowViewModel, &UnitRowViewModel::pick>(*this);
}

void UnitRowViewModel::pick() {
    picked_ = true;
}

}
