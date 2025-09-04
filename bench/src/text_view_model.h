#pragma once

#include "paragraph_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// Fields of assets/ui/text.xml: paragraphs at three sizes and the identical lines.
class TextViewModel final : public engine::ui::ViewModel {
public:
    TextViewModel();

    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> large;
    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> medium;
    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> small;
    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> lines;
};

}
