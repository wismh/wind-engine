#pragma once

#include "group_view_model.h"
#include "method_command.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace bench {

// One collapsible inspector section: the header button toggles the body's `display` through `var-display`.
class SectionViewModel final : public engine::ui::ViewModel {
public:
    explicit SectionViewModel(std::string title_text);

    void toggle_body();
    void set_collapsed(bool collapsed);
    [[nodiscard]] bool collapsed() const { return collapsed_; }

    engine::ui::Bindable<std::string> title;
    engine::ui::Bindable<std::string> bodyDisplay;
    engine::ui::BindableList<std::shared_ptr<GroupViewModel>> groups;
    MethodCommand toggle;

private:
    bool collapsed_ = false;
};

}
