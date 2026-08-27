#include "motion_view_model.h"

#include <asset_ids.h>

namespace bench {

MotionViewModel::MotionViewModel() {
    assets::ui::Motion::bind(*this);
}

}
