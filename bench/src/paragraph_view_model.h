#pragma once

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <string>

namespace bench {

// One generated line or paragraph (`text`), shared by the text and clip scenes.
class ParagraphViewModel final : public engine::ui::ViewModel {
public:
    explicit ParagraphViewModel(std::string paragraph);

    engine::ui::Bindable<std::string> text;
};

}
