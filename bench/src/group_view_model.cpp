#include "group_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

GroupViewModel::GroupViewModel(std::string title_text) : title(std::move(title_text)) {
    assets::ui::Inspector::Sections::Groups::bind(*this);
    reset.bind_to<GroupViewModel, &GroupViewModel::reset_values>(*this);
    apply.bind_to<GroupViewModel, &GroupViewModel::apply_values>(*this);
}

void GroupViewModel::reset_values() {
    applied_ = 0;
}

void GroupViewModel::apply_values() {
    ++applied_;
}

}
