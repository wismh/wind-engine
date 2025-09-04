#pragma once

#include "bool_row_view_model.h"
#include "method_command.h"
#include "number_row_view_model.h"
#include "text_row_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>

namespace bench {

// One property group of an inspector section: a title, text, numeric, and flag rows, and two action buttons.
class GroupViewModel final : public engine::ui::ViewModel {
public:
    explicit GroupViewModel(std::string title_text);

    void reset_values();
    void apply_values();

    engine::ui::Bindable<std::string> title;
    engine::ui::BindableList<std::shared_ptr<TextRowViewModel>> textRows;
    engine::ui::BindableList<std::shared_ptr<NumberRowViewModel>> numberRows;
    engine::ui::BindableList<std::shared_ptr<BoolRowViewModel>> boolRows;
    MethodCommand reset;
    MethodCommand apply;

private:
    int applied_ = 0;
};

}
