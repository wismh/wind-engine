#include "build_view_model.h"

#include <asset_ids.h>

#include <engine/ui/binding_id.h>

namespace editor {

BuildViewModel::BuildViewModel() {
    assets::ui::Build::bind(*this);
    // Generated bind() does not register scroll-x / scroll-y bindings.
    property(engine::ui::intern("logScroll"), logScroll);
}

}
