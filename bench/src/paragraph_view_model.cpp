#include "paragraph_view_model.h"

#include <engine/ui/binding_id.h>

#include <utility>

namespace bench {

ParagraphViewModel::ParagraphViewModel(std::string paragraph) : text(std::move(paragraph)) {
    property(engine::ui::intern("text"), text);
}

}
