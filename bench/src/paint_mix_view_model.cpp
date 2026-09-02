#include "paint_mix_view_model.h"

#include <engine/ui/binding_id.h>

namespace bench {

PaintMixViewModel::PaintMixViewModel() {
    property(engine::ui::intern("rows"), rows);
}

}
