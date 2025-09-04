#pragma once

#include "paragraph_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// One inner ScrollView of the clip scene: its `lines`.
class PaneViewModel final : public engine::ui::ViewModel {
public:
    PaneViewModel();

    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> lines;
};

}
