#pragma once

#include "pane_row_view_model.h"
#include "paragraph_view_model.h"

#include <engine/ui/bindable.h>
#include <engine/ui/view_model.h>

#include <memory>

namespace bench {

// Fields of assets/ui/clip.xml: the notes column, the rows of panes, and the lines of the rotated block's two panes.
class ClipViewModel final : public engine::ui::ViewModel {
public:
    ClipViewModel();

    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> notes;
    engine::ui::BindableList<std::shared_ptr<PaneRowViewModel>> paneRows;
    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> tiltLines;
    engine::ui::BindableList<std::shared_ptr<ParagraphViewModel>> innerLines;
};

}
