#include "section_view_model.h"

#include <asset_ids.h>

#include <utility>

namespace bench {

SectionViewModel::SectionViewModel(std::string title_text)
    : title(std::move(title_text))
    , bodyDisplay(std::string("block")) {
    assets::ui::Inspector::Sections::bind(*this);
    toggle.bind_to<SectionViewModel, &SectionViewModel::toggle_body>(*this);
}

void SectionViewModel::toggle_body() {
    set_collapsed(!collapsed_);
}

void SectionViewModel::set_collapsed(bool collapsed) {
    collapsed_ = collapsed;
    bodyDisplay = std::string(collapsed ? "none" : "block");
}

}
