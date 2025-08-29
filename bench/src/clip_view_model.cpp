#include "clip_view_model.h"

#include <asset_ids.h>

namespace bench {

ClipViewModel::ClipViewModel() {
    assets::ui::Clip::bind(*this);
}

}
