#include "profiler_view_model.h"

#include <asset_ids.h>

namespace editor {

ProfilerViewModel::ProfilerViewModel() {
    assets::ui::Profiler::bind(*this);
}

}
