#include "profiler_view_model.h"

#include <asset_ids.h>

#include <engine/ui/binding_id.h>

namespace editor {

ProfilerViewModel::ProfilerViewModel() {
    assets::ui::Profiler::bind(*this);
    paint(engine::ui::intern("chart"), chart);
    paint(engine::ui::intern("shared"), shared);
}

}
